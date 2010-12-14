/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef KVSTORE_HH
#define KVSTORE_HH 1

#include <map>
#include <string>
#include <utility>

#include "item.hh"

/**
 * Result of database mutation operations.
 *
 * This is a pair where .first is the number of rows affected, and
 * .second is the ID that was generated (if any).  .second will be 0
 * on updates (not generating an ID).
 *
 * .first will be -1 if there was an error performing the update.
 *
 * .first will be 0 if the update did not error, but did not occur.
 * This would generally be considered a fatal condition (in practice,
 * it requires you to be firing an update at a missing rowid).
 */
typedef std::pair<int, int64_t> mutation_result;

/**
 * Properites of the storage layer.
 *
 * If concurrent filesystem access is possible, maxConcurrency() will
 * be greater than one.  One will need to determine whether more than
 * one writer is possible as well as whether more than one reader is
 * possible.
 */
class StorageProperties {
public:

    StorageProperties(size_t c, size_t r, size_t w)
        : maxc(c), maxr(r), maxw(w) {}

    //! The maximum number of active queries.
    size_t maxConcurrency() { return maxc; }
    //! Maximum number of active read-only connections.
    size_t maxReaders()     { return maxr; }
    //! Maximum number of active connections for read and write.
    size_t maxWriters()     { return maxw; }

private:
    size_t maxc;
    size_t maxr;
    size_t maxw;
};

/**
 * Base class representing kvstore operations.
 */
class KVStore {
public:

    virtual ~KVStore() {}

    /**
     * Reset the store to a clean state.
     */
    virtual void reset() = 0;

    /**
     * Begin a transaction (if not already in one).
     *
     * @return false if we cannot begin a transaction
     */
    virtual bool begin() = 0;

    /**
     * Commit a transaction (unless not currently in one).
     *
     * @return false if the commit fails
     */
    virtual bool commit() = 0;

    /**
     * Rollback the current transaction.
     */
    virtual void rollback() = 0;

    /**
     * Get the properties of the underlying storage.
     */
    virtual StorageProperties getStorageProperties() = 0;

    /**
     * Set an item into the kv store.
     */
    virtual void set(const Item &item, uint16_t vb_version,
                     Callback<mutation_result> &cb) = 0;

    /**
     * Get an item from the kv store.
     */
    virtual void get(const std::string &key, uint64_t rowid,
                     Callback<GetValue> &cb) = 0;

    /**
     * Delete an item from the kv store.
     */
    virtual void del(const std::string &key, uint64_t rowid,
                     Callback<int> &cb) = 0;

    /**
     * Bulk delete some versioned records from a vbucket.
     */
    virtual bool delVBucket(uint16_t vbucket, uint16_t vb_version,
                            std::pair<int64_t, int64_t> row_range) = 0;

    /**
     * Get a list of all persisted vbuckets (with their versions and states).
     */
    virtual std::map<std::pair<uint16_t, uint16_t>, std::string> listPersistedVbuckets(void) = 0;

    /**
     * Persist a snapshot of a collection of stats.
     */
    virtual bool snapshotStats(const std::map<std::string, std::string> &m) = 0;

    /**
     * Snapshot vbucket states.
     */
    virtual bool snapshotVBuckets(const std::map<std::pair<uint16_t, uint16_t>, std::string> &m) = 0;

    /**
     * Pass all stored data through the given callback.
     */
    virtual void dump(Callback<GetValue> &cb) = 0;

};

#endif // KVSTORE_HH
