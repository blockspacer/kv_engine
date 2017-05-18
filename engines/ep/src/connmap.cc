/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include <phosphor/phosphor.h>

#include "connmap.h"
#include "executorthread.h"
#include "tapconnection.h"
#include "dcp/backfill-manager.h"
#include "dcp/consumer.h"
#include "dcp/producer.h"

size_t ConnMap::vbConnLockNum = 32;
const double ConnNotifier::DEFAULT_MIN_STIME = 1.0;

/**
 * A Callback task for connection notifier
 */
class ConnNotifierCallback : public GlobalTask {
public:
    ConnNotifierCallback(EventuallyPersistentEngine* e, ConnNotifier* notifier)
        : GlobalTask(e, TaskId::ConnNotifierCallback),
          connNotifier(notifier),
          description(connNotifier->getNotifierType() == TAP_CONN_NOTIFIER
                              ? "TAP connection notifier"
                              : "DCP connection notifier") {
    }


    bool run(void) {
        return connNotifier->notifyConnections();
    }

    cb::const_char_buffer getDescription() {
        return description;
    }

private:
    ConnNotifier *connNotifier;
    const cb::const_char_buffer description;
};

void ConnNotifier::start() {
    bool inverse = false;
    pendingNotification.compare_exchange_strong(inverse, true);
    ExTask connotifyTask = new ConnNotifierCallback(&connMap.getEngine(), this);
    task = ExecutorPool::get()->schedule(connotifyTask);
}

void ConnNotifier::stop() {
    bool inverse = true;
    pendingNotification.compare_exchange_strong(inverse, false);
    ExecutorPool::get()->cancel(task);
}

void ConnNotifier::notifyMutationEvent(void) {
    bool inverse = false;
    if (pendingNotification.compare_exchange_strong(inverse, true)) {
        if (task > 0) {
            ExecutorPool::get()->wake(task);
        }
    }
}

void ConnNotifier::wake() {
    ExecutorPool::get()->wake(task);
}

bool ConnNotifier::notifyConnections() {
    bool inverse = true;
    pendingNotification.compare_exchange_strong(inverse, false);
    connMap.notifyAllPausedConnections();

    if (!pendingNotification.load()) {
        ExecutorPool::get()->snooze(task, DEFAULT_MIN_STIME);
        if (pendingNotification.load()) {
            // Check again if a new notification is arrived right before
            // calling snooze() above.
            ExecutorPool::get()->snooze(task, 0);
        }
    }

    return true;
}

/**
 * A task to manage connections.
 */
class ConnManager : public GlobalTask {
public:
    ConnManager(EventuallyPersistentEngine *e, ConnMap *cmap)
        : GlobalTask(e, TaskId::ConnManager,
                     e->getConfiguration().getConnectionManagerInterval(),
                     true),
          engine(e), connmap(cmap),
          snoozeTime(e->getConfiguration().getConnectionManagerInterval()) { }

    /**
     * The ConnManager task is used to run the manageConnections function
     * once a second.  This is required for two reasons:
     * (1) To clean-up dead connections
     * (2) To notify idle connections; either for connections that need to be
     * closed or to ensure dcp noop messages are sent once a second.
     */
    bool run(void) {
        TRACE_EVENT0("ep-engine/task", "ConnManager");
        connmap->manageConnections();
        snooze(snoozeTime);
        return !engine->getEpStats().isShutdown ||
               connmap->isConnections() ||
               !connmap->isDeadConnectionsEmpty();
    }

    cb::const_char_buffer getDescription() {
        return "Connection Manager";
    }

private:
    static const double sleepTime;
    EventuallyPersistentEngine *engine;
    ConnMap *connmap;
    size_t snoozeTime;
};

const double ConnManager::sleepTime = 1.0;

ConnMap::ConnMap(EventuallyPersistentEngine &theEngine)
    :  engine(theEngine),
       connNotifier_(nullptr) {

    Configuration &config = engine.getConfiguration();
    vbConnLocks = new SpinLock[vbConnLockNum];
    size_t max_vbs = config.getMaxVbuckets();
    for (size_t i = 0; i < max_vbs; ++i) {
        vbConns.push_back(std::list<connection_t>());
    }
}

void ConnMap::initialize(conn_notifier_type ntype) {
    connNotifier_ = new ConnNotifier(ntype, *this);
    connNotifier_->start();
    ExTask connMgr = new ConnManager(&engine, this);
    ExecutorPool::get()->schedule(connMgr);
}

ConnMap::~ConnMap() {
    delete [] vbConnLocks;
    if (connNotifier_) {
        connNotifier_->stop();
        delete connNotifier_;
    }
}

void ConnMap::notifyPausedConnection(connection_t conn, bool schedule) {
    if (engine.getEpStats().isShutdown) {
        return;
    }

    Notifiable* tp = dynamic_cast<Notifiable*>(conn.get());
    if (schedule) {
        if (tp && tp->isPaused() && conn->isReserved() &&
            tp->setNotificationScheduled(true)) {
            pendingNotifications.push(conn);
            if (connNotifier_) {
                // Wake up the connection notifier so that
                // it can notify the event to a given paused connection.
                connNotifier_->notifyMutationEvent();
            }
        }
    } else {
        LockHolder rlh(releaseLock);
        if (tp && tp->isPaused() && conn->isReserved()) {
            engine.notifyIOComplete(conn->getCookie(), ENGINE_SUCCESS);
            tp->setNotifySent(true);
        }
    }
}

void ConnMap::notifyAllPausedConnections() {
    std::queue<connection_t> queue;
    pendingNotifications.getAll(queue);

    LockHolder rlh(releaseLock);
    while (!queue.empty()) {
        connection_t &conn = queue.front();
        Notifiable *tp = dynamic_cast<Notifiable*>(conn.get());
        if (tp) {
            tp->setNotificationScheduled(false);
            if (tp->isPaused() && conn->isReserved()) {
                engine.notifyIOComplete(conn->getCookie(), ENGINE_SUCCESS);
                tp->setNotifySent(true);
            }
        }
        queue.pop();
    }
}

bool ConnMap::notificationQueueEmpty() {
    return pendingNotifications.empty();
}

void ConnMap::updateVBConnections(connection_t &conn,
                                        const std::vector<uint16_t> &vbuckets)
{
    Producer *tp = dynamic_cast<Producer*>(conn.get());
    if (!tp) {
        return;
    }

    VBucketFilter new_filter(vbuckets);
    VBucketFilter diff = tp->getVBucketFilter().filter_diff(new_filter);
    const std::set<uint16_t> &vset = diff.getVBSet();

    for (std::set<uint16_t>::const_iterator it = vset.begin(); it != vset.end(); ++it) {
        size_t lock_num = (*it) % vbConnLockNum;
        std::lock_guard<SpinLock> lh(vbConnLocks[lock_num]);
        // Remove the connection that is no longer for a given vbucket
        if (!tp->vbucketFilter.empty() && tp->vbucketFilter(*it)) {
            std::list<connection_t> &vb_conns = vbConns[*it];
            std::list<connection_t>::iterator itr = vb_conns.begin();
            for (; itr != vb_conns.end(); ++itr) {
                if (conn->getCookie() == (*itr)->getCookie()) {
                    vb_conns.erase(itr);
                    break;
                }
            }
        } else { // Add the connection to the vbucket replicator list.
            std::list<connection_t> &vb_conns = vbConns[*it];
            vb_conns.push_back(conn);
        }
    }
}

void ConnMap::removeVBConnections(connection_t &conn) {
    Producer *tp = dynamic_cast<Producer*>(conn.get());
    if (!tp) {
        return;
    }

    const std::set<uint16_t> &vset = tp->vbucketFilter.getVBSet();
    for (std::set<uint16_t>::const_iterator it = vset.begin(); it != vset.end(); ++it) {
        size_t lock_num = (*it) % vbConnLockNum;
        std::lock_guard<SpinLock> lh(vbConnLocks[lock_num]);
        std::list<connection_t> &vb_conns = vbConns[*it];
        std::list<connection_t>::iterator itr = vb_conns.begin();
        for (; itr != vb_conns.end(); ++itr) {
            if (conn->getCookie() == (*itr)->getCookie()) {
                vb_conns.erase(itr);
                break;
            }
        }
    }
}

void ConnMap::addVBConnByVBId(connection_t &conn, int16_t vbid) {
    if (!conn.get()) {
        return;
    }

    size_t lock_num = vbid % vbConnLockNum;
    std::lock_guard<SpinLock> lh(vbConnLocks[lock_num]);
    std::list<connection_t> &vb_conns = vbConns[vbid];
    vb_conns.push_back(conn);
}

void ConnMap::removeVBConnByVBId_UNLOCKED(connection_t &conn, int16_t vbid) {
    if (!conn.get()) {
        return;
    }

    std::list<connection_t> &vb_conns = vbConns[vbid];
    std::list<connection_t>::iterator itr = vb_conns.begin();
    for (; itr != vb_conns.end(); ++itr) {
        if (conn->getCookie() == (*itr)->getCookie()) {
            vb_conns.erase(itr);
            break;
        }
    }
}

void ConnMap::removeVBConnByVBId(connection_t &conn, int16_t vbid) {
    size_t lock_num = vbid % vbConnLockNum;
    std::lock_guard<SpinLock> lh(vbConnLocks[lock_num]);
    removeVBConnByVBId_UNLOCKED(conn, vbid);
}
