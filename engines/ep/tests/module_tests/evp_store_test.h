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
 * Unit tests for the EPBucket class.
 *
 * These are instantiated for value and full eviction persistent buckets.
 */

#pragma once

#include "config.h"

#include "kv_bucket_test.h"

/**
 * Test fixture for EPBucket unit tests.
 */
class EPBucketTest : public KVBucketTest {
    // Note this class is currently identical to it's parent class as the
    // default bucket_type in configuration.json is EPBucket, therefore
    // KVBucketTest already defaults to creating EPBucket. Introducing this
    // subclass to just make the name more descriptive.
};