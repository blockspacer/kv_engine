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

#include "globaltask.h"

class TestTask : public GlobalTask {
public:
    TestTask(EventuallyPersistentEngine* e, TaskId id, int o = 0)
        : GlobalTask(e, id, 0.0, false),
          order(o),
          description(std::string("TestTask ") +
                      GlobalTask::getTaskName(getTypeId())) {
    }

    // returning true will also drive the ExecutorPool::reschedule path.
    bool run() { return true; }

    cb::const_char_buffer getDescription() {
        return description;
    }

    int order;
    const std::string description;
};
