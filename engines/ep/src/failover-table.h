/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2014 Couchbase, Inc
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

#include "utility.h"

#include <cJSON.h>
#include <memcached/engine.h>
#include <platform/random.h>

#include <atomic>
#include <list>
#include <mutex>
#include <string>

typedef struct {
    uint64_t vb_uuid;
    uint64_t by_seqno;
} failover_entry_t;

/**
 * The failover table hold a list of uuid/sequence number pairs. The sequence
 * numbers are always guaranteed to be increasing. This table is used to
 * detect changes of history caused by node failures.
 */
class FailoverTable {
 public:
    typedef std::list<failover_entry_t> table_t;

    FailoverTable(size_t capacity);

    FailoverTable(const std::string& json, size_t capacity);

    ~FailoverTable();

    /**
     * Returns the latest entry in the failover table
     */
    failover_entry_t getLatestEntry() const;

    /**
     * Remove the latest entry from the failover table
     */
    void removeLatestEntry();

    /**
     * Returns the cached version of the latest UUID
     */
    uint64_t getLatestUUID() const;

    /**
     * Creates a new entry in the table
     *
     * Calling this function with the same high sequence number does not change
     * the state of the failover table. If this function is called with a lower
     * sequence number than what exists in the table then all entries with a
     * higher sequence number are removed from the table.
     *
     * @param the high sequence number to create an entry with
     */
    void createEntry(uint64_t high_sequence);

    /**
     * Retrieves the last sequence number seen for a particular vbucket uuid
     *
     * @param uuid  the vbucket uuid
     * @param seqno the last sequence number seen for given vbucket uuid
     * @return true if the last sequence number seen of a given UUID is
     *              retrieved from the failover log
     */
    bool getLastSeqnoForUUID(uint64_t uuid, uint64_t *seqno) const;

    /**
     * Finds a rollback point based on the failover log of a remote client
     *
     * If this failover table contains an entry that matches the vbucket
     * uuid/high sequence number pair passed into this function and the start
     * sequence number is between the sequence number of the matching entry and
     * then sequence number of the following entry then no rollback is needed.
     * If no entry is found for the passed vbucket uuid/high sequence number
     * pair then a rollback to 0 is required.
     *
     * One special case of rollback is if the start sequence number is 0. In
     * this case we never need a rollback since we are starting from the
     * beginning of the data file.
     *
     * @param start_seqno the seq number the remote client wants to start from
     * @param cur_seqno the current source sequence number for this vbucket
     * @param vb_uuid the latest vbucket uuid of the remote client
     * @param snap_start_seqno the start seq number of the sanpshot
     * @param snap_end_seqno the end seq number of the sanpshot
     * @param purge_seqno last seq no purged during compaction
     * @param rollback_seqno the sequence number to rollback to if necessary
     * @return true and reason if a rollback is needed, false otherwise
     */
    std::pair<bool, std::string> needsRollback(uint64_t start_seqno,
                                               uint64_t cur_seqno,
                                               uint64_t vb_uuid,
                                               uint64_t snap_start_seqno,
                                               uint64_t snap_end_seqno,
                                               uint64_t purge_seqno,
                                               uint64_t* rollback_seqno) const;

    /**
     * Delete all entries in failover table uptil the specified sequence
     * number. Used after rollback is completed.
     */
    void pruneEntries(uint64_t seqno);

    /**
     * Converts the failover table to a json string
     *
     * @return a representation of the failover table in json format
     */
    std::string toJSON();

    /**
     * Adds stats for this failover table
     *
     * @param cookie the connection object requesting stats
     * @param vbid the vbucket id to use in the stats output
     * @param add_stat the callback used to add stats
     */
    void addStats(const void* cookie, uint16_t vbid, ADD_STAT add_stat);

    /**
     * Adds the failover table to a response
     *
     * @param cookie the connection object requesting stats
     * @param callback the callback used to add the failover table
     */
    ENGINE_ERROR_CODE addFailoverLog(const void* cookie,
                                     dcp_add_failover_log callback);


    void replaceFailoverLog(uint8_t* bytes, uint32_t length);

    /**
     * Returns total number of entries in the failover table. These entries
     * represent a branch
     *
     * @return total number of entries
     */
    size_t getNumEntries() const;

    /**
     * Returns total number of erroneous entries that were erased from the
     * failover table.
     *
     * @return total number of entries
     */
    size_t getNumErroneousEntriesErased() const;

 private:

    bool loadFromJSON(cJSON *json);
    bool loadFromJSON(const std::string& json);
    void cacheTableJSON();

    /**
     * DCP consumer being in middle of a snapshot is one of the reasons for rollback.
     * By updating the snap_start_seqno and snap_end_seqno appropriately we can
     * avoid unnecessary rollbacks.
     *
     * @param start_seqno the sequence number that a consumer wants to start with
     * @param snap_start_seqno the start sequence number of the snapshot
     * @param snap_end_seqno the end sequence number of the snapshot
     */
    static void adjustSnapshotRange(uint64_t start_seqno,
                                    uint64_t &snap_start_seqno,
                                    uint64_t &snap_end_seqno);

    /**
     * Remove any wrong entries in failover table
     *
     * called only in ctor, hence does not grab lock
     */
    void sanitizeFailoverTable();

    mutable std::mutex lock;
    table_t table;
    size_t max_entries;
    size_t erroneousEntriesErased;
    Couchbase::RandomGenerator provider;
    std::string cachedTableJSON;
    std::atomic<uint64_t> latest_uuid;

    friend std::ostream& operator<<(std::ostream& os,
                                    const FailoverTable& table);
    DISALLOW_COPY_AND_ASSIGN(FailoverTable);
};

std::ostream& operator<<(std::ostream& os, const failover_entry_t& entry);
std::ostream& operator<<(std::ostream& os, const FailoverTable& table);
