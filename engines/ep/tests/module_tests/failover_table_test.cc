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

#include "config.h"
#include "failover-table.h"

#include <gtest/gtest.h>

#include <limits>

typedef std::list<failover_entry_t> table_t;

//create entries in the Failover table under test with
//specified no of entries and seqno hint
static table_t generate_entries(FailoverTable& table,
                                int numentries,
                                uint64_t seqno_range){

    table_t failover_entries;

    for(int i=0;i<numentries;i++){
        table.createEntry(i*150*seqno_range);
        failover_entries.push_front(table.getLatestEntry());
    }
    return failover_entries;
}

//TESTS BELOW
TEST(FailoverTableTest, test_initial_failover_log) {
    uint64_t rollback_seqno;
    FailoverTable table(25);

    // rollback not needed
    EXPECT_FALSE(table.needsRollback(0, 0, 0, 0, 0, 0, &rollback_seqno).first);

    // rollback needed
    EXPECT_TRUE(table.needsRollback(10, 0, 0, 0, 0, 0, &rollback_seqno).first);
    EXPECT_EQ(0, rollback_seqno);
}

TEST(FailoverTableTest, test_5_failover_log) {
    uint64_t rollback_seqno;
    uint64_t curr_seqno;

    FailoverTable table(25);
    table_t failover_entries = generate_entries(table, 5,1);

    // rollback not needed
    EXPECT_FALSE(table.needsRollback(0, 0, 0, 0, 0, 0, &rollback_seqno).first);

    curr_seqno = table.getLatestEntry().by_seqno + 100;
    EXPECT_FALSE(table.needsRollback(10,
                                     curr_seqno,
                                     table.getLatestEntry().vb_uuid,
                                     0,
                                     20,
                                     0,
                                     &rollback_seqno)
                         .first);

    // rollback needed
    EXPECT_TRUE(table.needsRollback(10, 0, 0, 0, 0, 0, &rollback_seqno).first);
    EXPECT_EQ(0, rollback_seqno);

    curr_seqno = table.getLatestEntry().by_seqno + 100;
    EXPECT_TRUE(table.needsRollback(curr_seqno - 80,
                                    curr_seqno,
                                    table.getLatestEntry().vb_uuid,
                                    0,
                                    curr_seqno + 20,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(0, rollback_seqno);
}

TEST(FailoverTableTest, test_edgetests_failover_log) {
    uint64_t start_seqno;
    uint64_t snap_start_seqno;
    uint64_t snap_end_seqno;
    uint64_t rollback_seqno;
    uint64_t curr_seqno;

    FailoverTable table(25);
    table.createEntry(100);
    table.createEntry(200);

    table_t failover_entries = generate_entries(table, 5,1);

    //TESTS for rollback not needed
    EXPECT_FALSE(table.needsRollback(0, 0, 0, 0, 0, 0, &rollback_seqno).first);

    //start_seqno == snap_start_seqno == snap_end_seqno and start_seqno < upper
    curr_seqno = 300;
    start_seqno = 200;
    snap_start_seqno = 200;
    snap_end_seqno = 200;

    EXPECT_FALSE(table.needsRollback(start_seqno,
                                     curr_seqno,
                                     table.getLatestEntry().vb_uuid,
                                     snap_start_seqno,
                                     snap_end_seqno,
                                     0,
                                     &rollback_seqno)
                         .first);

    //start_seqno == snap_start_seqno and snap_end_seqno > upper
    curr_seqno = 300;
    start_seqno = 200;
    snap_start_seqno = 200;
    snap_end_seqno = 301;

    EXPECT_FALSE(table.needsRollback(start_seqno,
                                     curr_seqno,
                                     table.getLatestEntry().vb_uuid,
                                     snap_start_seqno,
                                     snap_end_seqno,
                                     0,
                                     &rollback_seqno)
                         .first);

    //start_seqno == snap_start_seqno == upper and snap_end_seqno > upper
    curr_seqno = 300;
    start_seqno = 300;
    snap_start_seqno = 300;
    snap_end_seqno = 301;

    EXPECT_FALSE(table.needsRollback(start_seqno,
                                     curr_seqno,
                                     table.getLatestEntry().vb_uuid,
                                     snap_start_seqno,
                                     snap_end_seqno,
                                     0,
                                     &rollback_seqno)
                         .first);

    //TEST for rollback needed

    //start_seqno == snap_start_seqno == snap_end_seqno and start_seqno > upper
    curr_seqno = 300;
    start_seqno = 400;
    snap_start_seqno = 400;
    snap_end_seqno = 400;

    EXPECT_TRUE(table.needsRollback(start_seqno,
                                    curr_seqno,
                                    table.getLatestEntry().vb_uuid,
                                    snap_start_seqno,
                                    snap_end_seqno,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(curr_seqno, rollback_seqno);

    //start_seqno > snap_start_seqno and snap_end_seqno > upper
    curr_seqno = 300;
    start_seqno = 220;
    snap_start_seqno = 210;
    snap_end_seqno = 301;

    EXPECT_TRUE(table.needsRollback(start_seqno,
                                    curr_seqno,
                                    table.getLatestEntry().vb_uuid,
                                    snap_start_seqno,
                                    snap_end_seqno,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(snap_start_seqno, rollback_seqno);


    //start_seqno > upper and snap_end_seqno > upper
    curr_seqno = 300;
    start_seqno = 310;
    snap_start_seqno = 210;
    snap_end_seqno = 320;

    EXPECT_TRUE(table.needsRollback(start_seqno,
                                    curr_seqno,
                                    table.getLatestEntry().vb_uuid,
                                    snap_start_seqno,
                                    snap_end_seqno,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(snap_start_seqno, rollback_seqno);
}

TEST(FailoverTableTest, test_5_failover_largeseqno_log) {
    uint64_t start_seqno;
    uint64_t rollback_seqno;
    uint64_t curr_seqno;
    uint64_t vb_uuid;

    FailoverTable table(25);
    uint64_t range = std::numeric_limits<uint64_t>::max()/(5*150);
    table_t failover_entries = generate_entries(table, 5, range);

    //TESTS for rollback not needed
    EXPECT_FALSE(table.needsRollback(0, 0, 0, 0, 0, 0, &rollback_seqno).first);

    vb_uuid = table.getLatestEntry().vb_uuid;
    curr_seqno = table.getLatestEntry().by_seqno + 100;
    start_seqno = 10;
    //snapshot end seqno less than upper
    EXPECT_FALSE(table.needsRollback(start_seqno,
                                     curr_seqno,
                                     vb_uuid,
                                     0,
                                     20,
                                     0,
                                     &rollback_seqno)
                         .first);

    //TESTS for rollback needed
    EXPECT_TRUE(table.needsRollback(10, 0, 0, 0, 0, 0, &rollback_seqno).first);
    EXPECT_EQ(0, rollback_seqno);

    //vbucket uuid sent by client not present in failover table
    EXPECT_TRUE(
            table.needsRollback(
                         start_seqno, curr_seqno, 0, 0, 20, 0, &rollback_seqno)
                    .first);
    EXPECT_EQ(0, rollback_seqno);

    vb_uuid = table.getLatestEntry().vb_uuid;
    curr_seqno = table.getLatestEntry().by_seqno + 100;
    start_seqno = curr_seqno-80;
    //snapshot sequence no is greater than upper && snapshot start sequence no
    // less than upper
    EXPECT_TRUE(table.needsRollback(start_seqno,
                                    curr_seqno,
                                    vb_uuid,
                                    curr_seqno - 20,
                                    curr_seqno + 20,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ((curr_seqno-20), rollback_seqno);
    //snapshot start seqno greate than  upper
    EXPECT_TRUE(table.needsRollback(curr_seqno + 20,
                                    curr_seqno,
                                    vb_uuid,
                                    curr_seqno + 10,
                                    curr_seqno + 40,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(curr_seqno, rollback_seqno);
    //client vb uuiud is not the latest vbuuid in failover table and
    //snap_end_seqno > upper && snap_start_seqno > upper
    std::list<failover_entry_t>::iterator itr = failover_entries.begin();
    ++itr;
    vb_uuid = itr->vb_uuid;
    --itr;
    EXPECT_TRUE(table.needsRollback(itr->by_seqno - 5,
                                    curr_seqno,
                                    vb_uuid,
                                    itr->by_seqno - 10,
                                    itr->by_seqno + 40,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(((itr->by_seqno)-10), rollback_seqno);
    //client vb uuiud is not the latest vbuuid in failover table and
    //snapshot start seqno greate than  upper
    EXPECT_TRUE(table.needsRollback(itr->by_seqno + 20,
                                    curr_seqno,
                                    vb_uuid,
                                    itr->by_seqno + 10,
                                    itr->by_seqno + 40,
                                    0,
                                    &rollback_seqno)
                        .first);
    EXPECT_EQ(itr->by_seqno, rollback_seqno);
}

TEST(FailoverTableTest, test_pop_5_failover_log) {
    uint64_t rollback_seqno;

    FailoverTable table(25);
    table_t failover_entries = generate_entries(table, 30,1);


    //Verify seq no. in latest entry
    EXPECT_EQ(29*150, table.getLatestEntry().by_seqno);
    EXPECT_EQ(failover_entries.front().by_seqno, table.getLatestEntry().by_seqno);

    // rollback not needed
    EXPECT_FALSE(table.needsRollback(0, 0, 0, 0, 0, 0, &rollback_seqno).first);

    // rollback needed
    EXPECT_TRUE(table.needsRollback(10, 0, 0, 0, 0, 0, &rollback_seqno).first);
    EXPECT_EQ(0, rollback_seqno);
}

TEST(FailoverTableTest, test_add_entry) {
    /* Capacity of max 10 entries */
    const int max_entries = 10;
    FailoverTable table(max_entries);

    /* Add 4 entries with increasing order of seqno */
    const int low_seqno = 100, step = 100;
    for (int i = 0; i < (max_entries/2); ++i) {
        table.createEntry(low_seqno + i * step);
    }

    /* We must have all the entries we added + one default entry with seqno == 0
       that was added when we created failover table */
    EXPECT_EQ((max_entries/2 + 1), table.getNumEntries());

    /* Add an entry such that low_seqno < seqno < low_seqno + step.
       Now the table must have only 3 entries: 0, low_seqno, seqno */
    table.createEntry(low_seqno + step - 1);
    EXPECT_EQ(3, table.getNumEntries());
}

TEST(FailoverTableTest, rollback_log_messages) {
    /* Doesn't actually test functionality, just allows manual confirmation of
     * the logged messages */

    uint64_t rollback_seqno;

    FailoverTable table(25);

    table_t failover_entries = generate_entries(table, 1, 50);

    uint64_t vb_uuid = table.getLatestEntry().vb_uuid;

    LOG(EXTENSION_LOG_WARNING,
        "%s",
        table.needsRollback(10, 0, 0, 0, 0, 20, &rollback_seqno)
                .second.c_str());
    LOG(EXTENSION_LOG_WARNING,
        "%s",
        table.needsRollback(10, 0, 0, 0, 0, 0, &rollback_seqno).second.c_str());
    LOG(EXTENSION_LOG_WARNING,
        "%s",
        table.needsRollback(10, 0, vb_uuid, 0, 100, 0, &rollback_seqno)
                .second.c_str());
    LOG(EXTENSION_LOG_WARNING,
        "%s",
        table.needsRollback(10, 15, vb_uuid, 20, 100, 0, &rollback_seqno)
                .second.c_str());
}

TEST(FailoverTableTest, test_max_capacity) {
    /* Capacity of max 5 entries */
    const int max_entries = 5;
    FailoverTable table(max_entries);

    const int low_seqno = 100, step = 100;
    for (int i = 0; i < max_entries + 2; ++i) {
        table.createEntry(low_seqno + i * step);
    }
    const int max_seqno = low_seqno + (max_entries + 1) * step;

    /* Expect to have only max num of entries */
    EXPECT_EQ(max_entries, table.getNumEntries());

    /* The table must remove entries in FIFO */
    EXPECT_EQ(max_seqno, table.getLatestEntry().by_seqno);
}

TEST(FailoverTableTest, test_sanitize_failover_table) {
    const int numErroneousEntries = 4, numCorrectEntries = 2;
    std::string failover_json(/* Erroneous entry */
                              "[{\"id\":0,\"seq\":0},"
                              "{\"id\":1356861809263,\"seq\":100},"
                              /* Erroneous entry */
                              "{\"id\":227813077095126,\"seq\":200},"
                              /* Erroneous entry */
                              "{\"id\":227813077095128,\"seq\":300},"
                              /* Erroneous entry */
                              "{\"id\":0,\"seq\":50},"
                              "{\"id\":160260368866392,\"seq\":0}]");
    FailoverTable table(failover_json, 10 /* max_entries */);

    EXPECT_EQ(numCorrectEntries, table.getNumEntries());
    EXPECT_EQ(numErroneousEntries, table.getNumErroneousEntriesErased());
}
