/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
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
#include "daemon/mcbp.h"
#include "daemon/memcached.h"
#include "engine_wrapper.h"
#include "mutation_context.h"

#include <memcached/protocol_binary.h>
#include <memcached/types.h>
#include <xattr/utils.h>

MutationCommandContext::MutationCommandContext(Cookie& cookie,
                                               const cb::mcbp::Request& req,
                                               const ENGINE_STORE_OPERATION op_)
    : SteppableCommandContext(cookie),
      operation(req.getCas() == 0 ? op_ : OPERATION_CAS),
      key(cookie.getRequestKey()),
      value(req.getValue()),
      vbucket(req.getVBucket()),
      input_cas(req.getCas()),
      expiration(ntohl(reinterpret_cast<const protocol_binary_request_set&>(req)
                               .message.body.expiration)),
      flags(reinterpret_cast<const protocol_binary_request_set&>(req)
                    .message.body.flags),
      datatype(req.datatype),
      state(State::ValidateInput),
      store_if_predicate(cookie.getConnection().selectedBucketIsXattrEnabled()
                                 ? storeIfPredicate
                                 : nullptr) {
}

ENGINE_ERROR_CODE MutationCommandContext::step() {
    auto ret = ENGINE_SUCCESS;
    do {
        switch (state) {
        case State::ValidateInput:
            ret = validateInput();
            break;
        case State::GetExistingItemToPreserveXattr:
            ret = getExistingItemToPreserveXattr();
            break;
        case State::AllocateNewItem:
            ret = allocateNewItem();
            break;
        case State::StoreItem:
            ret = storeItem();
            break;
        case State::SendResponse:
            ret = sendResponse();
            break;
        case State::Reset:
            ret = reset();
            break;
        case State::Done:
            if (operation == OPERATION_CAS) {
                SLAB_INCR(&connection, cas_hits);
            } else {
                SLAB_INCR(&connection, cmd_set);
            }
            return ENGINE_SUCCESS;
        }
    } while (ret == ENGINE_SUCCESS);

    if (ret != ENGINE_EWOULDBLOCK) {
        if (operation == OPERATION_CAS) {
            switch (ret) {
            case ENGINE_KEY_EEXISTS:
                SLAB_INCR(&connection, cas_badval);
                break;
            case ENGINE_KEY_ENOENT:
                get_thread_stats(&connection)->cas_misses++;
                break;
            default:;
            }
        } else {
            SLAB_INCR(&connection, cmd_set);
        }
    }

    return ret;
}

ENGINE_ERROR_CODE MutationCommandContext::validateInput() {
    if (!connection.isDatatypeEnabled(datatype)) {
        return ENGINE_EINVAL;
    }

    /**
     * If snappy datatype is enabled and if the datatype is SNAPPY,
     * validate to data to ensure that it is compressed using SNAPPY
     */
    try {
        if (mcbp::datatype::is_snappy(datatype)) {
            cb::const_char_buffer value_buf{reinterpret_cast<const char*>(value.buf),
                                            value.len};

            if (!cb::compression::inflate(cb::compression::Algorithm::Snappy,
                                          value_buf,
                                          decompressed_value)) {
                return ENGINE_EINVAL;
            }

            setDatatypeJSONFromValue(decompressed_value, datatype);

            const auto mode = bucket_get_compression_mode(cookie);
            if (mode == BucketCompressionMode::Off) {
                value.buf = reinterpret_cast<const uint8_t*>(
                        decompressed_value.data());
                value.len = decompressed_value.size();
                datatype &= ~PROTOCOL_BINARY_DATATYPE_SNAPPY;
            }
        } else {
            // Determine if document is JSON or not. We do not trust what the client
            // sent - instead we check for ourselves.
            setDatatypeJSONFromValue(value, datatype);
        }
    } catch (const std::bad_alloc&) {
        return ENGINE_ENOMEM;
    }

    state = State::AllocateNewItem;
    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE MutationCommandContext::getExistingItemToPreserveXattr() {
    // Try to fetch the previous version of the document _iff_ it contains
    // any xattrs so that we can preserve those by copying them over to
    // the new document. Documents without any xattrs can safely be
    // ignored. The motivation to use get_if over a normal get is for the
    // value eviction case where the underlying engine would have to read
    // the value off disk in order to return it via get() even if we don't
    // need it (and would throw it away in the frontend).
    auto pair = bucket_get_if(cookie, key, vbucket, [](const item_info& info) {
        return mcbp::datatype::is_xattr(info.datatype);
    });
    if (pair.first != cb::engine_errc::no_such_key &&
        pair.first != cb::engine_errc::success) {
        return ENGINE_ERROR_CODE(pair.first);
    }

    existing = std::move(pair.second);
    if (!existing) {
        state = State::AllocateNewItem;
        return ENGINE_SUCCESS;
    }

    if (!bucket_get_item_info(cookie, existing.get(), &existing_info)) {
        return ENGINE_FAILED;
    }

    if (input_cas != 0) {
        if (existing_info.cas == uint64_t(-1)) {
            // The object in the cache is locked... lets try to use
            // the cas provided by the user to override this
            existing_info.cas = input_cas;
        } else if (input_cas != existing_info.cas) {
            return ENGINE_KEY_EEXISTS;
        }
    } else if (existing_info.cas == uint64_t(-1)) {
        return ENGINE_LOCKED;
    }

    // Found the existing item (with it's XATTRs) - create a read-only
    // view on them. Note in the case the existing item is compressed;
    // we'll decompress as part of creating the Blob.
    cb::char_buffer existingValue{
            static_cast<char*>(existing_info.value[0].iov_base),
            existing_info.value[0].iov_len};
    existingXattrs.assign(existingValue,
                          mcbp::datatype::is_snappy(existing_info.datatype));

    state = State::AllocateNewItem;

    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE MutationCommandContext::allocateNewItem() {
    auto dtype = datatype;
    if (existingXattrs.size() > 0) {
        // We need to prepend the existing XATTRs - include XATTR bit
        // in datatype:
        dtype |= PROTOCOL_BINARY_DATATYPE_XATTR;
        // The result will also *not* be compressed - even if the
        // input value was (as we combine the data uncompressed).
        dtype &= ~PROTOCOL_BINARY_DATATYPE_SNAPPY;
    }

    size_t total_size = value.size() + existingXattrs.size();
    if (existingXattrs.size() > 0 && mcbp::datatype::is_snappy(datatype)) {
        total_size = decompressed_value.size() + existingXattrs.size();
    }

    item_info newitem_info;
    try {
        auto ret = bucket_allocate_ex(cookie,
                                      key,
                                      total_size,
                                      existingXattrs.get_system_size(),
                                      flags,
                                      expiration,
                                      dtype,
                                      vbucket);
        if (!ret.first) {
            return ENGINE_ENOMEM;
        }

        newitem = std::move(ret.first);
        newitem_info = ret.second;
    } catch (const cb::engine_error &e) {
        return ENGINE_ERROR_CODE(e.code().value());
    }

    if (operation == OPERATION_ADD || input_cas != 0) {
        bucket_item_set_cas(cookie, newitem.get(), input_cas);
    } else {
        if (existing) {
            bucket_item_set_cas(cookie, newitem.get(), existing_info.cas);
        } else {
            bucket_item_set_cas(cookie, newitem.get(), input_cas);
        }
    }

    auto* root = reinterpret_cast<uint8_t*>(newitem_info.value[0].iov_base);
    if (existingXattrs.size() > 0) {
        // Preserve the xattrs
        std::copy(existingXattrs.data(),
                  existingXattrs.data() + existingXattrs.size(),
                  root);
        root += existingXattrs.size();
    }

    // Copy the user supplied value over. If the user-supplied value
    // was Snappy and we have XATTRs, we must use the decompressed
    // version of it (compression is only applied to the complete
    // value+XATTR pair, not to only part of it).
    if (existingXattrs.size() > 0 && mcbp::datatype::is_snappy(datatype)) {
        std::copy(decompressed_value.data(),
                  decompressed_value.data() + decompressed_value.size(),
                  root);
    } else {
        std::copy(value.begin(), value.end(), root);
    }
    state = State::StoreItem;

    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE MutationCommandContext::storeItem() {
    auto ret = bucket_store_if(
            cookie, newitem.get(), input_cas, operation, store_if_predicate);
    if (ret.status == cb::engine_errc::success) {
        cookie.setCas(ret.cas);
        state = State::SendResponse;
    } else if (ret.status == cb::engine_errc::predicate_failed) {
        // predicate failed because xattrs are present
        state = State::GetExistingItemToPreserveXattr;

        // Mark as success and we'll move to the next state
        ret.status = cb::engine_errc::success;

        // Next time we store - we force it
        store_if_predicate = nullptr;
    } else if (ret.status == cb::engine_errc::not_stored) {
        // Need to remap error for add and replace
        if (operation == OPERATION_ADD) {
            ret.status = cb::engine_errc::key_already_exists;
        } else if (operation == OPERATION_REPLACE) {
            ret.status = cb::engine_errc::no_such_key;
        }
    } else if (ret.status == cb::engine_errc::key_already_exists &&
               input_cas == 0) {
        // We failed due to CAS mismatch, and the user did not specify
        // the CAS, retry the operation.
        state = State::Reset;
        ret.status = cb::engine_errc::success;
    }

    return ENGINE_ERROR_CODE(ret.status);
}

ENGINE_ERROR_CODE MutationCommandContext::sendResponse() {
    update_topkeys(cookie);
    state = State::Done;

    if (cookie.getRequest().isQuiet()) {
        ++connection.getBucket()
                  .responseCounters[PROTOCOL_BINARY_RESPONSE_SUCCESS];
        connection.setState(McbpStateMachine::State::new_cmd);
        return ENGINE_SUCCESS;
    }

    mutation_descr_t mutation_descr{};
    cb::const_char_buffer extras{};

    if (connection.isSupportsMutationExtras()) {
        item_info newitem_info;
        if (!bucket_get_item_info(cookie, newitem.get(), &newitem_info)) {
            return ENGINE_FAILED;
        }

        // Response includes vbucket UUID and sequence number
        // (in addition to value)
        mutation_descr.vbucket_uuid = htonll(newitem_info.vbucket_uuid);
        mutation_descr.seqno = htonll(newitem_info.seqno);
        extras = {reinterpret_cast<const char*>(&mutation_descr),
                  sizeof(mutation_descr)};
    }

    cookie.sendResponse(cb::mcbp::Status::Success,
                        extras,
                        {},
                        {},
                        cb::mcbp::Datatype::Raw,
                        cookie.getCas());

    return ENGINE_SUCCESS;
}

ENGINE_ERROR_CODE MutationCommandContext::reset() {
    newitem.reset();
    existing.reset();
    existingXattrs.assign({nullptr, 0}, false);
    state = State::GetExistingItemToPreserveXattr;
    return ENGINE_SUCCESS;
}

// predicate so that we fail if any existing item has
// an xattr datatype. In the case an item may not be in cache (existing
// is not initialised) we force a fetch (return GetInfo) if the VB may
// have xattr items in it.
cb::StoreIfStatus MutationCommandContext::storeIfPredicate(
        const boost::optional<item_info>& existing, cb::vbucket_info vb) {
    if (existing.is_initialized()) {
        if (mcbp::datatype::is_xattr(existing.value().datatype)) {
            return cb::StoreIfStatus::Fail;
        }
    } else if (vb.mayContainXattrs) {
        return cb::StoreIfStatus::GetItemInfo;
    }
    return cb::StoreIfStatus::Continue;
}
