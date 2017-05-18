/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc
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

#include "vbucketdeletiontask.h"
#include "ep_engine.h"
#include "ep_vb.h"
#include "executorpool.h"
#include "kvshard.h"

#include <phosphor/phosphor.h>
#include <platform/processclock.h>

VBucketMemoryDeletionTask::VBucketMemoryDeletionTask(
        EventuallyPersistentEngine& eng, VBucket* vb, TaskId tid)
    : GlobalTask(&eng, tid, 0.0, true), vbucket(vb) {
    if (!vbucket) {
        throw std::logic_error(
                "VBucketMemoryDeletionTask::VBucketMemoryDeletionTask no "
                "vbucket");
    }
    description = "Removing (dead) vb:" + std::to_string(vbucket->getId()) +
                  " from memory";
}

cb::const_char_buffer VBucketMemoryDeletionTask::getDescription() {
    return description;
}

bool VBucketMemoryDeletionTask::run() {
    TRACE_EVENT(
            "ep-engine/task", "VBucketMemoryDeletionTask", vbucket->getId());

    notifyAllPendingConnsFailed(true);

    return false;
}

void VBucketMemoryDeletionTask::notifyAllPendingConnsFailed(
        bool notifyIfCookieSet) {
    vbucket->notifyAllPendingConnsFailed(*engine);

    if (notifyIfCookieSet && vbucket->getDeferredDeletionCookie()) {
        engine->notifyIOComplete(vbucket->getDeferredDeletionCookie(),
                                 ENGINE_SUCCESS);
    }
}

VBucketMemoryAndDiskDeletionTask::VBucketMemoryAndDiskDeletionTask(
        EventuallyPersistentEngine& eng, KVShard& shard, EPVBucket* vb)
    : VBucketMemoryDeletionTask(eng,
                                static_cast<VBucket*>(vb),
                                TaskId::VBucketMemoryAndDiskDeletionTask),
      shard(shard),
      vbDeleteRevision(vb->getDeferredDeletionFileRevision()) {
    description += " and disk";
}

bool VBucketMemoryAndDiskDeletionTask::run() {
    TRACE_EVENT("ep-engine/task",
                "VBucketMemoryAndDiskDeletionTask",
                vbucket->getId());
    notifyAllPendingConnsFailed(false);

    auto start = ProcessClock::now();
    shard.getRWUnderlying()->delVBucket(vbucket->getId(), vbDeleteRevision);
    auto elapsed = ProcessClock::now() - start;
    auto wallTime =
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed);

    engine->getEpStats().vbucketDeletions++;
    BlockTimer::log(
            elapsed.count(), "disk_vb_del", engine->getEpStats().timingLog);
    engine->getEpStats().diskVBDelHisto.add(wallTime.count());
    atomic_setIfBigger(engine->getEpStats().vbucketDelMaxWalltime,
                       hrtime_t(wallTime.count()));
    engine->getEpStats().vbucketDelTotWalltime.fetch_add(wallTime.count());

    if (vbucket->getDeferredDeletionCookie()) {
        engine->notifyIOComplete(vbucket->getDeferredDeletionCookie(),
                                 ENGINE_SUCCESS);
    }

    return false;
}
