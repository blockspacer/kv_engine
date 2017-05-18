/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011 Couchbase, Inc
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

#ifndef SRC_BACKFILL_H_
#define SRC_BACKFILL_H_ 1

#include "config.h"

#include <string>

#include "globaltask.h"
#include "kv_bucket_iface.h"

#define DEFAULT_BACKFILL_SNOOZE_TIME 1.0

class EventuallyPersistentEngine;
class TapConnMap;

enum backfill_t {
    ALL_MUTATIONS = 1,
    DELETIONS_ONLY
};

/**
 * Dispatcher callback responsible for bulk backfilling tap queues
 * from a KVStore.
 *
 * Note that this is only used if the KVStore reports that it has
 * efficient vbucket ops.
 */
class BackfillDiskLoad : public GlobalTask {
public:

    BackfillDiskLoad(const std::string &n, EventuallyPersistentEngine* e,
                     TapConnMap &cm, KVStore *s, uint16_t vbid,
                     uint64_t start_seqno, hrtime_t token,
                     double sleeptime = 0, bool shutdown = false);

    bool run();

    cb::const_char_buffer getDescription();

private:
    const std::string           name;
    const std::string description;
    EventuallyPersistentEngine *engine;
    TapConnMap                    &connMap;
    KVStore                    *store;
    uint16_t                    vbucket;
    uint64_t                    startSeqno;
    hrtime_t                    connToken;
};

/**
 * VBucketVisitor to backfill a Producer. This visitor basically performs
 * backfill from memory for only resident items if it needs to schedule a
 * separate disk backfill task because of low resident ratio.
 *
 * The visitor will pause if the current backfill backlog for the corresponding
 * producer is greater than the threshold (5000 by default).
 */
class BackFillVisitor : public VBucketVisitor {
public:
    BackFillVisitor(EventuallyPersistentEngine *e, TapConnMap &cm, Producer *tc,
                    const VBucketFilter &backfillVBfilter);

    virtual ~BackFillVisitor() {}

    void visitBucket(VBucketPtr &vb) override;

    void complete(void) override;

private:

    bool pauseVisitor() override;

    bool checkValidity();

    EventuallyPersistentEngine *engine;
    TapConnMap &connMap;
    const std::string name;
    hrtime_t connToken;
    bool valid;
};

#endif  // SRC_BACKFILL_H_
