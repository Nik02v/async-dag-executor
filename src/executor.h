#pragma once

#include "task_graph.h"

#include <condition_variable>
#include <exception>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

enum class TaskState { PENDING, READY, RUNNING, SUCCEEDED, FAILED, SKIPPED };

const char* state_name(TaskState state);

struct TaskResult {
    TaskState state = TaskState::PENDING;
    std::exception_ptr error;
};

struct RunReport {

    std::vector<TaskResult> tasks;
};

class Executor {
public:
    explicit Executor(std::size_t thread_count);
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    std::future<RunReport> run(TaskGraph graph);

private:
    void worker_loop();
    void skip_descendants(TaskId id);
    void complete_if_done();

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    std::queue<TaskId> ready;
    TaskGraph graph;
    RunReport report;
    std::vector<std::size_t> remaining;
    std::vector<TaskId> skip_stack;
    std::promise<RunReport> promise;
    std::size_t finished = 0;
    bool started = false;
    bool stopping = false;
};
