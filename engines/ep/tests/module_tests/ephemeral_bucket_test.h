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

/**
 * Unit tests for the EphemeralBucket class.
 */

#include "config.h"

#include "stats_test.h"

/**
 * Ephemeral Bucket test fixture class for statistics tests.
 */
class EphemeralBucketStatTest : public StatTest {
protected:
    void SetUp() override {
        config_string += "bucket_type=ephemeral";
        StatTest::SetUp();
    }

    /// Add a number of documents to allow testing of sequence list stats.
    void addDocumentsForSeqListTesting(uint16_t vbid);
};
