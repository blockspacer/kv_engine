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

#include <nlohmann/json_fwd.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "collections/collections_types.h"
#include "memcached/engine_common.h"

namespace Collections {

static const size_t MaxCollectionNameSize = 30;

struct CollectionEntry {
    CollectionID id;
    cb::ExpiryLimit maxTtl;
};

struct Scope {
    std::string name;
    std::vector<CollectionEntry> collections;
};

/**
 * Manifest is an object that is constructed from JSON data as per
 * a set_collections command
 *
 * Users of this class can then obtain the UID and all collections that are
 * included in the manifest.
 */
class Manifest {
public:
    /*
     * Create a manifest from json.
     * Validates the json as per SET_COLLECTIONS rules.
     * @param json a buffer containing the JSON manifest data
     * @param maxNumberOfScopes an upper limit on the number of scopes allowed
     *        defaults to 100.
     * @param maxNumberOfCollections an upper limit on the number of collections
     *        allowed, defaults to 1000.
     */
    Manifest(cb::const_char_buffer json,
             size_t maxNumberOfScopes = 100,
             size_t maxNumberOfCollections = 1000);

    Manifest(const std::string& json,
             size_t maxNumberOfScopes = 100,
             size_t maxNumberOfCollections = 1000)
        : Manifest(cb::const_char_buffer{json},
                   maxNumberOfScopes,
                   maxNumberOfCollections) {
    }

    bool doesDefaultCollectionExist() const {
        return defaultCollectionExists;
    }

    // This manifest object stores UID to Scope mappings
    using scopeContainer = std::unordered_map<ScopeID, Scope>;

    // This manifest object stores CID to name mappings for collections
    using collectionContainer = std::unordered_map<CollectionID, std::string>;

    collectionContainer::const_iterator begin() const {
        return collections.begin();
    }

    collectionContainer::const_iterator end() const {
        return collections.end();
    }

    scopeContainer::const_iterator beginScopes() const {
        return scopes.begin();
    }

    scopeContainer::const_iterator endScopes() const {
        return scopes.end();
    }

    size_t size() const {
        return collections.size();
    }

    /**
     * @return the unique ID of the Manifest which constructed this
     */
    ManifestUid getUid() const {
        return uid;
    }

    /**
     * Search for a collection by CollectionID
     *
     * @param cid CollectionID to search for.
     * @return iterator to the matching entry or end() if not found.
     */
    collectionContainer::const_iterator findCollection(CollectionID cid) const {
        return collections.find(cid);
    }

    /**
     * Search for a collection by name (requires a scope name also)
     *
     * @param collectionName Name of the collection to search for.
     * @param scopeName Name of the scope in which to search. Defaults to the
     * default scope.
     * @return iterator to the matching entry or end() if not found.
     */
    collectionContainer::const_iterator findCollection(
            const std::string& collectionName,
            const std::string& scopeName =
                    cb::to_string(DefaultScopeIdentifier)) const {
        for (auto& scope : scopes) {
            if (scope.second.name == scopeName) {
                for (auto& scopeCollection : scope.second.collections) {
                    auto collection = collections.find(scopeCollection.id);

                    if (collection != collections.end() &&
                        collection->second == collectionName) {
                        return collection;
                    }
                }
            }
        }

        return collections.end();
    }

    /**
     * Search for a scope by ScopeID
     *
     * @param sid ScopeID to search for.
     * @return iterator to the matching entry or end() if not found.
     */
    scopeContainer::const_iterator findScope(ScopeID sid) const {
        return scopes.find(sid);
    }

    /**
     * Attempt to lookup the collection-id of the "path" note that this method
     * skips/ignores the scope part of the path and requires the caller to
     * specify the scope for the actual ID lookup. getScopeID(path) exists for
     * this purpose.
     *
     * A path defined as "scope.collection"
     *
     * _default collection can be specified by name or by omission
     * e.g. "." == "_default._default"
     *      "c1." == "c1._default" (which would fail to find an ID)
     *
     * @param scope The scopeID of the scope part of the path
     * @param path The full path, the scope part is not used
     * @return optional CollectionID, undefined if nothing found
     * @throws cb::engine_error(invalid_argument) for invalid input
     */
    boost::optional<CollectionID> getCollectionID(
            ScopeID scope, const std::string& path) const;

    /**
     * Attempt to lookup the scope-id of the "path", note that this method
     * ignores the collection part of the path.
     *
     * A path defined as either "scope.collection" or "scope"
     *
     * _default scope can be specified by name or by omission
     * e.g. ".beer" == _default scope
     *      ".      == _default scope
     *      ""      == _default scope
     *
     * @return optional ScopeID, undefined if nothing found
     * @throws cb::engine_error(invalid_argument) for invalid input
     */
    boost::optional<ScopeID> getScopeID(const std::string& path) const;

    /**
     * @returns this manifest as a std::string (JSON formatted)
     */
    std::string toJson() const;

    void addCollectionStats(const void* cookie,
                            const AddStatFn& add_stat) const;

    void addScopeStats(const void* cookie, const AddStatFn& add_stat) const;

    /**
     * Write to std::cerr this
     */
    void dump() const;

private:
    /**
     * Set defaultCollectionExists to true if identifier matches
     * CollectionID::Default
     * @param identifier ID to check
     */
    void enableDefaultCollection(CollectionID identifier);

    /**
     * Check if the std::string represents a legal collection name.
     * Current validation is to ensure we block creation of _ prefixed
     * collections and only accept $default for $ prefixed names.
     *
     * @param name a std::string representing a collection or scope name.
     */
    static bool validName(const std::string& name);

    /**
     * Check if the CollectionID is invalid for a Manifest
     */
    static bool invalidCollectionID(CollectionID identifier);

    friend std::ostream& operator<<(std::ostream& os, const Manifest& manifest);

    bool defaultCollectionExists;
    scopeContainer scopes;
    collectionContainer collections;
    ManifestUid uid;
};

std::ostream& operator<<(std::ostream& os, const Manifest& manifest);

} // end namespace Collections
