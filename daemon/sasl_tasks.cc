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
#include "sasl_tasks.h"
#include "buckets.h"
#include "connection.h"
#include "cookie.h"
#include "mcaudit.h"
#include "memcached.h"
#include <cbsasl/mechanism.h>
#include <cbsasl/server.h>
#include <logger/logger.h>
#include <memcached/engine.h>
#include <memcached/rbac.h>
#include <utilities/logtags.h>

SaslAuthTask::SaslAuthTask(Cookie& cookie_,
                           Connection& connection_,
                           const std::string& mechanism_,
                           const std::string& challenge_)
    : cookie(cookie_),
      connection(connection_),
      serverContext(connection_.getSaslConn()),
      mechanism(mechanism_),
      challenge(challenge_) {
    // no more init needed
}

void SaslAuthTask::notifyExecutionComplete() {
    connection.setAuthenticated(false);
    using PrivilegeContext = cb::rbac::PrivilegeContext;
    using Domain = cb::sasl::Domain;
    std::pair<PrivilegeContext, bool> context{PrivilegeContext{Domain::Local},
                                              false};

    // If CBSASL generated a UUID, we should continue to use that UUID
    if (serverContext.containsUuid()) {
        cookie.setEventId(serverContext.getUuid());
    }

    if (response.first == cb::sasl::Error::OK) {
        // Authentication successful, but it still has to be defined in
        // our system
        try {
            context = cb::rbac::createInitialContext(connection.getUsername(),
                                                     connection.getDomain());
        } catch (const cb::rbac::NoSuchUserException&) {
            response.first = cb::sasl::Error::NO_RBAC_PROFILE;
        }
    }

    // Perform the appropriate logging for each error code
    switch (response.first) {
    case cb::sasl::Error::OK:
        // Success
        connection.setAuthenticated(true);
        connection.setInternal(context.second);
        audit_auth_success(connection);
        LOG_INFO("{}: Client {} authenticated as {}",
                 connection.getId(),
                 connection.getPeername(),
                 cb::UserDataView(connection.getUsername()));

        /* associate the connection with the appropriate bucket */
        {
            std::string username = connection.getUsername();
            auto idx = username.find(";legacy");
            if (idx != username.npos) {
                username.resize(idx);
            }

            if (cb::rbac::mayAccessBucket(connection.getUsername(),
                                          connection.getDomain(),
                                          username)) {
                associate_bucket(connection, username.c_str());
                // Auth succeeded but the connection may not be valid for the
                // bucket
                if (connection.isCollectionsSupported() &&
                    !connection.getBucket().supports(
                            cb::engine::Feature::Collections)) {
                    // Move back to the "no bucket" as this is not valid
                    associate_bucket(connection, "");
                }
            } else {
                // the user don't have access to that bucket, move the
                // connection to the "no bucket"
                associate_bucket(connection, "");
            }
        }
        break;
    case cb::sasl::Error::CONTINUE:
        LOG_DEBUG("{}: SASL CONTINUE", connection.getId());
        break;
    case cb::sasl::Error::FAIL:
    case cb::sasl::Error::BAD_PARAM:
    case cb::sasl::Error::NO_MEM:
        // Should already have been logged
        break;
    case cb::sasl::Error::NO_MECH:
        cookie.setErrorContext("Requested mechanism \"" + mechanism +
            "\" is not supported");
        break;
    case cb::sasl::Error::NO_USER:
        LOG_WARNING("{}: User [{}] not found. UUID:[{}]",
                    connection.getId(),
                    cb::UserDataView(connection.getUsername()),
                    cookie.getEventId());
        break;

    case cb::sasl::Error::PASSWORD_ERROR:
        LOG_WARNING("{}: Invalid password specified for [{}] UUID:[{}]",
                    connection.getId(),
                    cb::UserDataView(connection.getUsername()),
                    cookie.getEventId());
        break;
    case cb::sasl::Error::NO_RBAC_PROFILE:
        LOG_WARNING(
                "{}: User [{}] is not defined as a user in Couchbase. "
                "UUID:[{}]",
                connection.getId(),
                cb::UserDataView(connection.getUsername()),
                cookie.getEventId());
        break;
    case cb::sasl::Error::AUTH_PROVIDER_DIED:
        LOG_WARNING("{}: Auth provider closed the connection. UUID:[{}]",
                    connection.getId(),
                    cookie.getEventId());
    }

    notify_io_complete(static_cast<void*>(&cookie), ENGINE_SUCCESS);
}
