/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc
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

/*
 * Unit tests for the EventuallyPersistentEngine class.
 */

#include "evp_engine_test.h"

#include "ep_engine.h"
#include "programs/engine_testapp/mock_server.h"
#include "tests/module_tests/test_helpers.h"

#include <configuration_impl.h>
#include <platform/dirutils.h>

void EventuallyPersistentEngineTest::SetUp() {
    // Paranoia - kill any existing files in case they are left over
    // from a previous run.
    cb::io::rmrf(test_dbname);

    // Setup an engine with a single active vBucket.
    EXPECT_EQ(ENGINE_SUCCESS,
              create_instance(1, get_mock_server_api, &handle))
        << "Failed to create ep engine instance";
    EXPECT_EQ(1, handle->interface) << "Unexpected engine handle version";
    engine_v1 = reinterpret_cast<ENGINE_HANDLE_V1*>(handle);

    engine = reinterpret_cast<EventuallyPersistentEngine*>(handle);
    ObjectRegistry::onSwitchThread(engine);

    // Add dbname to config string.
    std::string config = config_string;
    if (config.size() > 0) {
        config += ";";
    }
    config += "dbname=" + std::string(test_dbname);

    // Set the bucketType
    config += ";bucket_type=" + bucketType;

    EXPECT_EQ(ENGINE_SUCCESS, engine->initialize(config.c_str()))
        << "Failed to initialize engine.";

    // Wait for warmup to complete.
    while (engine->getKVBucket()->isWarmingUp()) {
        usleep(10);
    }

    // Once warmup is complete, set VB to active.
    engine->setVBucketState(vbid, vbucket_state_active, false);
}

void EventuallyPersistentEngineTest::TearDown() {
    // Need to force the destroy (i.e. pass true) because
    // NonIO threads may have been disabled (see DCPTest subclass).
    engine_v1->destroy(handle, true);
    destroy_mock_event_callbacks();
    destroy_engine();
    // Cleanup any files we created.
    cb::io::rmrf(test_dbname);
}

void EventuallyPersistentEngineTest::store_item(uint16_t vbid,
                                                const std::string& key,
                                                const std::string& value) {
    Item item(makeStoredDocKey(key), /*flags*/0, /*exp*/0, value.c_str(),
              value.size(), nullptr/*extmeta*/, 0/*extlen*/, 0/*cas*/,
              -1/*seqno*/, vbid);
    uint64_t cas;
    EXPECT_EQ(ENGINE_SUCCESS,
              engine->store(NULL, &item, &cas, OPERATION_SET));
}

const char EventuallyPersistentEngineTest::test_dbname[] = "ep_engine_ep_unit_tests_db";


TEST_P(SetParamTest, requirements_bucket_type) {
    std::string bucketType = engine->getConfiguration().getBucketType();

    struct value_t {
        std::string param;
        std::string value;
        std::string bucketType;
    };

    std::vector<value_t> values{
            // Parameter, Example value, applicable bucket
            {"access_scanner_enabled", "true", "persistent"},
            {"alog_sleep_time", "1441", "persistent"},
            {"alog_task_time", "3", "persistent"},
            {"ephemeral_full_policy", "auto_delete", "ephemeral"},
    };

    std::string msg;

    for (auto v : values) {
        auto ret = engine->setFlushParam(v.param.c_str(), v.value.c_str(), msg);
        if (bucketType == v.bucketType) {
            EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, ret)
                    << "Parameter " << v.param
                    << "could not be set on bucket type \"" << bucketType
                    << "\"";
        } else {
            EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EINVAL, ret)
                    << "Setting parameter " << v.param
                    << "should be invalid for bucket type \"" << bucketType
                    << "\"";
        }
    }
}

TEST_F(EventuallyPersistentEngineTest, requirements_tap) {
    Configuration& config = engine->getConfiguration();
    config.setTap(true);

    std::string msg;

    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS,
              engine->setTapParam("tap_keepalive", "0", msg))
            << "tap is enabled but \"tap_keepalive\" could not be set";

    config.setTap(false);

    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_EINVAL,
              engine->setTapParam("tap_keepalive", "0", msg))
            << "Setting \"tap_keepalive\" should be invalid if tap is disabled";
}

// Test cases which run for persistent and ephemeral buckets
INSTANTIATE_TEST_CASE_P(EphemeralOrPersistent,
                        SetParamTest,
                        ::testing::Values("persistent", "ephemeral"),
                        [](const ::testing::TestParamInfo<std::string>& info) {
                            return info.param;
                        });
