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

#include "configuration.h"

#include <atomic>
#include <string>

class BucketLogger;

class KVStoreConfig {
public:
    /**
     * This constructor intialises the object from a central
     * ep-engine Configuration instance.
     */
    KVStoreConfig(Configuration& config, uint16_t numShards, uint16_t shardId);

    /**
     * This constructor sets the mandatory config options
     *
     * Optional config options are set using a separate method
     */
    KVStoreConfig(uint16_t _maxVBuckets,
                  uint16_t _maxShards,
                  const std::string& _dbname,
                  const std::string& _backend,
                  uint16_t _shardId);

    virtual ~KVStoreConfig();

    uint16_t getMaxVBuckets() const {
        return maxVBuckets;
    }

    uint16_t getMaxShards() const {
        return maxShards;
    }

    std::string getDBName() const {
        return dbname;
    }

    const std::string& getBackend() const {
        return backend;
    }

    uint16_t getShardId() const {
        return shardId;
    }

    BucketLogger& getLogger() {
        return *logger;
    }

    /**
     * Indicates whether or not underlying file operations will be
     * buffered by the storage engine used.
     *
     * Only recognised by CouchKVStore
     */
    bool getBuffered() const {
        return buffered;
    }

    /**
     * Used to override the default logger object
     */
    KVStoreConfig& setLogger(BucketLogger& _logger);

    /**
     * Used to override the default buffering behaviour.
     *
     * Only recognised by CouchKVStore
     */
    KVStoreConfig& setBuffered(bool _buffered);

    uint64_t getPeriodicSyncBytes() const {
        return periodicSyncBytes;
    }

    void setPeriodicSyncBytes(uint64_t bytes) {
        periodicSyncBytes = bytes;
    }
    void setCouchstoreTracingEnabled(bool value) {
        couchstoreTracingEnabled = value;
    }

    bool getCouchstoreTracingEnabled() const {
        return couchstoreTracingEnabled;
    }
    void setCouchstoreWriteValidationEnabled(bool value) {
        couchstoreWriteValidationEnabled = value;
    }

    bool getCouchstoreWriteValidationEnabled() const {
        return couchstoreWriteValidationEnabled;
    }

    void setCouchstoreMprotectEnabled(bool value) {
        couchstoreMprotectEnabled = value;
    }

    bool getCouchstoreMprotectEnabled() const {
        return couchstoreMprotectEnabled;
    }

private:
    class ConfigChangeListener;

    uint16_t maxVBuckets;
    uint16_t maxShards;
    std::string dbname;
    std::string backend;
    uint16_t shardId;
    BucketLogger* logger;
    bool buffered;

    // Following config variables are atomic as can be changed (via
    // ConfigChangeListener) at runtime by front-end threads while read by
    // IO threads.

    /**
     * If non-zero, tell storage layer to issue a sync() operation after every
     * N bytes written.
     */
    std::atomic<uint64_t> periodicSyncBytes;

    /* enable tracing for couchstore */
    std::atomic_bool couchstoreTracingEnabled;
    /* enable write verification for couchstore */
    std::atomic_bool couchstoreWriteValidationEnabled;
    /* enbale mprotect of couchstore internal io buffer */
    std::atomic_bool couchstoreMprotectEnabled;
};
