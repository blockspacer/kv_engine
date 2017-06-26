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

#pragma once

#include "config.h"

#include "callbacks.h"
#include "dcp/backfill.h"
#include "ephemeral_vb.h"

/**
 * Concrete class that does backfill from in-memory ordered data strucuture and
 * informs the DCP stream of the backfill progress.
 *
 * This class calls one synchronous vBucket API to read items in the sequential
 * order from the in-memory ordered data structure and calls the DCP stream
 * for disk snapshot, backfill items and backfill completion.
 */
class DCPBackfillMemory : public DCPBackfill {
public:
    DCPBackfillMemory(EphemeralVBucketPtr evb,
                      const active_stream_t& s,
                      uint64_t startSeqno,
                      uint64_t endSeqno);

    backfill_status_t run() override;

    bool isStreamDead() override {
        return !stream->isActive();
    }

    void cancel() override {
    }

private:
    /**
     * weak pointer to EphemeralVBucket
     */
    std::weak_ptr<EphemeralVBucket> weakVb;
};

/**
 * Concrete class that does backfill from in-memory ordered data strucuture and
 * informs the DCP stream of the backfill progress.
 *
 * This class calls one synchronous vBucket API to read items in the sequential
 * order from the in-memory ordered data structure and calls the DCP stream
 * for disk snapshot, backfill items and backfill completion.
 */
class DCPBackfillMemoryBuffered : public DCPBackfill {
public:
    DCPBackfillMemoryBuffered(EphemeralVBucketPtr evb,
                              const active_stream_t& s,
                              uint64_t startSeqno,
                              uint64_t endSeqno);

    ~DCPBackfillMemoryBuffered() override;

    backfill_status_t run() override;

    bool isStreamDead() override {
        return !stream->isActive();
    }

    void cancel() override;

private:
    /* The possible states of the DCPBackfillMemoryBuffered */
    enum class BackfillState : uint8_t { Init, Scanning, Done };

    static std::string backfillStateToString(BackfillState state);

    /**
     * Creates a range iterator on Ephemeral VBucket to read items as a snapshot
     * in sequential order. Backfill snapshot range is decided here.
     *
     * @param evb Ref to the ephemeral vbucket on which backfill is run
     */
    backfill_status_t create(EphemeralVBucket& evb);

    /**
     * Reads the items in the snapshot (iterator) one by one. In case of high
     * memory usage postpones the reading of items, and reading can be resumed
     * later on from that point.
     */
    backfill_status_t scan();

    /**
     * Indicates the completion to the stream.
     *
     * @param cancelled indicates if the backfill finished fully or was
     *                  cancelled in between; for debug
     */
    void complete(bool cancelled);

    /**
     * Makes valid transitions on the backfill state machine
     */
    void transitionState(BackfillState newState);

    /**
     * Ensures there can be no cyclic dependency with VB pointers in the
     * complex DCP slab of objects and tasks.
     */
    std::weak_ptr<EphemeralVBucket> weakVb;

    BackfillState state;

    /**
     * Range iterator (on the vbucket) created for the backfill
     */
    SequenceList::RangeIterator rangeItr;

    // VBucket ID, only used for debug / tracing.
    const VBucket::id_type vbid;
};
