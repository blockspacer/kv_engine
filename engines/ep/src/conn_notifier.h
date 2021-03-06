/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018 Couchbase, Inc
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

#include <atomic>
#include <memory>

class ConnMap;

/**
 * Connection notifier that wakes up paused connections.
 */
class ConnNotifier : public std::enable_shared_from_this<ConnNotifier> {
public:
    ConnNotifier(ConnMap& cm);

    void start();

    void stop();

    void notifyMutationEvent();

    bool notifyConnections();

private:
    ConnMap& connMap;
    std::atomic<size_t> task;
    std::atomic<bool> pendingNotification;
};
