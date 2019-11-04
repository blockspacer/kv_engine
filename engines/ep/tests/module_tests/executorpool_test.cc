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

/*
 * Unit tests for the ExecutorPool class
 */

#include "executorpool_test.h"
#include "lambda_task.h"

MockTaskable::MockTaskable() : policy(HIGH_BUCKET_PRIORITY, 1) {
}

const std::string& MockTaskable::getName() const {
    return name;
}

task_gid_t MockTaskable::getGID() const {
    return 0;
}

bucket_priority_t MockTaskable::getWorkloadPriority() const {
    return HIGH_BUCKET_PRIORITY;
}

void MockTaskable::setWorkloadPriority(bucket_priority_t prio) {
}

WorkLoadPolicy& MockTaskable::getWorkLoadPolicy(void) {
    return policy;
}

void MockTaskable::logQTime(TaskId id,
                            const std::chrono::steady_clock::duration enqTime) {
}

void MockTaskable::logRunTime(
        TaskId id, const std::chrono::steady_clock::duration runTime) {
}

ExTask makeTask(Taskable& taskable, ThreadGate& tg, size_t i) {
    return std::make_shared<LambdaTask>(
            taskable, TaskId::StatSnap, 0, true, [&]() -> bool {
                tg.threadUp();
                return false;
            });
}

::std::ostream& operator<<(::std::ostream& os,
                           const ExpectedThreadCounts& expected) {
    return os << "CPU" << expected.maxThreads << "_W" << expected.writer << "_R"
              << expected.reader << "_A" << expected.auxIO << "_N"
              << expected.nonIO;
}

TEST_F(ExecutorPoolTest, register_taskable_test) {
    TestExecutorPool pool(10, // MaxThreads
                          NUM_TASK_GROUPS,
                          2, // MaxNumReaders
                          2, // MaxNumWriters
                          2, // MaxNumAuxio
                          2 // MaxNumNonio
                          );

    MockTaskable taskable;
    MockTaskable taskable2;

    ASSERT_EQ(0, pool.getNumWorkersStat());
    ASSERT_EQ(0, pool.getNumBuckets());

    pool.registerTaskable(taskable);

    ASSERT_EQ(8, pool.getNumWorkersStat());
    ASSERT_EQ(1, pool.getNumBuckets());

    pool.registerTaskable(taskable2);

    ASSERT_EQ(8, pool.getNumWorkersStat());
    ASSERT_EQ(2, pool.getNumBuckets());

    pool.unregisterTaskable(taskable2, false);

    ASSERT_EQ(8, pool.getNumWorkersStat());
    ASSERT_EQ(1, pool.getNumBuckets());

    pool.unregisterTaskable(taskable, false);

    ASSERT_EQ(0, pool.getNumWorkersStat());
    ASSERT_EQ(0, pool.getNumBuckets());
}

/* This test creates an ExecutorPool, and attempts to verify that calls to
 * setNumWriters are able to dynamically create more workers than were present
 * at initialisation. A ThreadGate is used to confirm that two tasks
 * of type WRITER_TASK_IDX can run concurrently
 *
 */
TEST_F(ExecutorPoolTest, increase_workers) {
    const size_t numReaders = 1;
    const size_t numWriters = 1;
    const size_t numAuxIO = 1;
    const size_t numNonIO = 1;

    const size_t originalWorkers =
            numReaders + numWriters + numAuxIO + numNonIO;

    // This will allow us to check that numWriters + 1 writer tasks can run
    // concurrently after setNumWriters has been called.
    ThreadGate tg{numWriters + 1};

    TestExecutorPool pool(5, // MaxThreads
                          NUM_TASK_GROUPS,
                          numReaders,
                          numWriters,
                          numAuxIO,
                          numNonIO);

    MockTaskable taskable;
    pool.registerTaskable(taskable);

    std::vector<ExTask> tasks;

    for (size_t i = 0; i < numWriters + 1; ++i) {
        ExTask task = makeTask(taskable, tg, i);
        pool.schedule(task);
        tasks.push_back(task);
    }

    EXPECT_EQ(numWriters, pool.getNumWriters());
    ASSERT_EQ(originalWorkers, pool.getNumWorkersStat());

    pool.setNumWriters(numWriters + 1);

    EXPECT_EQ(numWriters + 1, pool.getNumWriters());
    ASSERT_EQ(originalWorkers + 1, pool.getNumWorkersStat());

    tg.waitFor(std::chrono::seconds(10));
    EXPECT_TRUE(tg.isComplete()) << "Timeout waiting for threads to run";

    pool.unregisterTaskable(taskable, false);
}

TEST_F(ExecutorPoolDynamicWorkerTest, decrease_workers) {
    EXPECT_EQ(2, pool->getNumWriters());
    pool->setNumWriters(1);
    EXPECT_EQ(1, pool->getNumWriters());
}

TEST_P(ExecutorPoolTestWithParam, max_threads_test_parameterized) {
    ExpectedThreadCounts expected = GetParam();

    MockTaskable taskable;

    TestExecutorPool pool(expected.maxThreads, // MaxThreads
                          NUM_TASK_GROUPS,
                          0, // MaxNumReaders (0 = use default)
                          0, // MaxNumWriters
                          0, // MaxNumAuxio
                          0 // MaxNumNonio
                          );

    pool.registerTaskable(taskable);

    EXPECT_EQ(expected.reader, pool.getNumReaders())
            << "When maxThreads=" << expected.maxThreads;
    EXPECT_EQ(expected.writer, pool.getNumWriters())
            << "When maxThreads=" << expected.maxThreads;
    EXPECT_EQ(expected.auxIO, pool.getNumAuxIO())
            << "When maxThreads=" << expected.maxThreads;
    EXPECT_EQ(expected.nonIO, pool.getNumNonIO())
            << "When maxThreads=" << expected.maxThreads;

    pool.unregisterTaskable(taskable, false);
    pool.shutdown();
}

std::vector<ExpectedThreadCounts> threadCountValues = {{1, 1, 2, 1, 2},
                                                       {2, 2, 2, 1, 2},
                                                       {4, 4, 4, 1, 2},
                                                       {8, 8, 8, 1, 2},
                                                       {10, 10, 10, 1, 3},
                                                       {14, 14, 14, 2, 4},
                                                       {20, 20, 20, 2, 6},
                                                       {24, 24, 24, 3, 7},
                                                       {32, 32, 32, 4, 8},
                                                       {48, 48, 48, 5, 8},
                                                       {64, 64, 64, 7, 8},
                                                       {128, 128, 128, 8, 8}};

INSTANTIATE_TEST_CASE_P(ThreadCountTest,
                        ExecutorPoolTestWithParam,
                        ::testing::ValuesIn(threadCountValues),
                        ::testing::PrintToStringParamName());

TEST_F(ExecutorPoolDynamicWorkerTest, new_worker_naming_test) {
    EXPECT_EQ(2, pool->getNumWriters());
    std::vector<std::string> names = pool->getThreadNames();

    EXPECT_TRUE(pool->threadExists("writer_worker_0"));
    EXPECT_TRUE(pool->threadExists("writer_worker_1"));

    pool->setNumWriters(1);

    EXPECT_TRUE(pool->threadExists("writer_worker_0"));
    EXPECT_FALSE(pool->threadExists("writer_worker_1"));

    pool->setNumWriters(2);

    EXPECT_TRUE(pool->threadExists("writer_worker_0"));
    EXPECT_TRUE(pool->threadExists("writer_worker_1"));
}

/* Make sure that a task that has run once and been cancelled can be
 * rescheduled and will run again properly.
 */
TEST_F(ExecutorPoolDynamicWorkerTest, reschedule_dead_task) {
    size_t runCount{0};

    ExTask task = std::make_shared<LambdaTask>(
            taskable, TaskId::ItemPager, 0, true, [&] {
                ++runCount;
                return false;
            });

    ASSERT_EQ(TASK_RUNNING, task->getState())
            << "Initial task state should be RUNNING";

    pool->schedule(task);
    pool->waitForEmptyTaskLocator();

    EXPECT_EQ(TASK_DEAD, task->getState())
            << "Task has completed and been cleaned up, state should be DEAD";

    pool->schedule(task);
    pool->waitForEmptyTaskLocator();

    EXPECT_EQ(TASK_DEAD, task->getState())
            << "Task has completed and been cleaned up, state should be DEAD";

    EXPECT_EQ(2, runCount);
}

/* Testing to ensure that repeatedly scheduling a task does not result in
 * multiple entries in the taskQueue - this could cause a deadlock in
 * _unregisterTaskable when the taskLocator is empty but duplicate tasks remain
 * in the queue.
 */
TEST_F(SingleThreadedExecutorPoolTest, ignore_duplicate_schedule) {
    ExTask task = std::make_shared<LambdaTask>(
            taskable, TaskId::ItemPager, 10, true, [&] { return false; });

    size_t taskId = task->getId();

    ASSERT_EQ(taskId, pool->schedule(task));
    ASSERT_EQ(taskId, pool->schedule(task));

    std::map<size_t, TaskQpair> taskLocator =
            dynamic_cast<SingleThreadedExecutorPool*>(ExecutorPool::get())
                    ->getTaskLocator();

    TaskQueue* queue = taskLocator.find(taskId)->second.second;

    EXPECT_EQ(1, queue->getFutureQueueSize())
            << "Task should only appear once in the taskQueue";

    pool->cancel(taskId, true);
}
