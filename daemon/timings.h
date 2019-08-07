/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
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

#include "timing_interval.h"

#include <mcbp/protocol/opcode.h>

#include <utilities/hdrhistogram.h>
#include <array>
#include <mutex>
#include <string>

#define MAX_NUM_OPCODES 0x100

/** Records timings for each memcached opcode. Each opcode has a histogram of
 * times.
 */
class Timings {
public:
    Timings();
    Timings(const Timings&) = delete;
    ~Timings();

    void reset();
    void collect(cb::mcbp::ClientOpcode opcode, std::chrono::nanoseconds nsec);
    void sample(std::chrono::seconds sample_interval);
    std::string generate(cb::mcbp::ClientOpcode opcode);
    uint64_t get_aggregated_mutation_stats();
    uint64_t get_aggregated_retrival_stats();

    cb::sampling::Interval get_interval_mutation_latency();
    cb::sampling::Interval get_interval_lookup_latency();

    /**
     * Get a pointer to the underlying histogram for the specified opcode
     * @return a pointer to the underling HdrMicroSecHistogram for this opcode
     * if there isn't one allocated already a nullptr will be returned.
     */
    Hdr1sfMicroSecHistogram* get_timing_histogram(uint8_t opcode) const;

private:
    /**
     * Method to get histogram for timing, if the histogram hasn't been created
     * yet, for the given opcode then we will allocate one
     */
    Hdr1sfMicroSecHistogram& get_or_create_timing_histogram(uint8_t opcode);

    // This lock is only held by sample() and some blocks within generate().
    // It guards the various IntervalSeries variables which internally
    // contain cb::RingBuffer objects which are not thread safe.
    std::mutex lock;

    cb::sampling::IntervalSeries interval_latency_lookups;
    cb::sampling::IntervalSeries interval_latency_mutations;
    // create an array of unique_ptrs as we want to create HdrHistograms
    // in a lazy manner as their foot print is larger than our old
    // histogram class
    std::array<std::atomic<Hdr1sfMicroSecHistogram*>, MAX_NUM_OPCODES> timings;
    std::mutex histogram_mutex;
    std::array<cb::sampling::Interval, MAX_NUM_OPCODES> interval_counters;
};
