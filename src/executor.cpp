#include "executor.h"

#include <stdexcept>
#include <utility>

const char* state_name(TaskState state)
{
    switch (state) {
    case TaskState::PENDING: return "PENDING";
    case TaskState::READY: return "READY";
    case TaskState::RUNNING: return "RUNNING";
    case TaskState::SUCCEEDED: return "SUCCEEDED";
    case TaskState::FAILED: return "FAILED";
    case TaskState::SKIPPED: return "SKIPPED";
    }
    return "?";
}

Executor::Executor(std::size_t thread_count)
{
    if (thread_count == 0)
        throw std::invalid_argument("Thread count must be positive");

    workers.reserve(thread_count);
    try {
        for (std::size_t i = 0; i < thread_count; ++i)
            workers.emplace_back(&Executor::worker_loop, this);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        for (auto& t : workers)
            t.join();
        throw;
    }
}

Executor::~Executor()
{
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!started)
            stopping = true;
    }
    cv.notify_all();
    for (auto& thread : workers)
        thread.join();
}

std::future<RunReport> Executor::run(TaskGraph input)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (started)
        throw std::logic_error("Executor supports only one run");

    input.validate();

    graph = std::move(input);
    report.tasks.resize(graph.size());
    remaining.assign(graph.size(), 0);
    skip_stack.reserve(graph.size());
    for (const auto& node : graph.nodes) {
        for (TaskId child : node.next)
            ++remaining[child];
    }

    std::queue<TaskId> initial;
    for (TaskId i = 0; i < graph.size(); ++i) {
        if (remaining[i] == 0) {
            initial.push(i);
            report.tasks[i].state = TaskState::READY;
        }
    }

    auto future = promise.get_future();
    ready.swap(initial);
    started = true;
    complete_if_done();
    cv.notify_all();
    return future;
}

void Executor::skip_descendants(TaskId id)
{
    skip_stack.clear();
    skip_stack.push_back(id);
    while (!skip_stack.empty()) {
        TaskId current = skip_stack.back();
        skip_stack.pop_back();

        for (TaskId child : graph.nodes[current].next) {
            if (report.tasks[child].state != TaskState::PENDING)
                continue;

            report.tasks[child].state = TaskState::SKIPPED;
            ++finished;
            skip_stack.push_back(child);
        }
    }
}

void Executor::complete_if_done()
{
    if (finished == graph.size()) {
        stopping = true;
        promise.set_value(std::move(report));
    }
}

void Executor::worker_loop()
{
    while (true) {
        TaskId id;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return stopping || !ready.empty(); });
            if (stopping)
                return;

            id = ready.front();
            ready.pop();
            report.tasks[id].state = TaskState::RUNNING;
        }

        std::exception_ptr error;
        try {
            graph.nodes[id].work();
        } catch (...) {
            error = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            ++finished;
            report.tasks[id].error = error;
            if (error) {
                report.tasks[id].state = TaskState::FAILED;
                skip_descendants(id);
            } else {
                report.tasks[id].state = TaskState::SUCCEEDED;
                for (TaskId child : graph.nodes[id].next) {
                    if (report.tasks[child].state != TaskState::PENDING)
                        continue;
                    --remaining[child];
                    if (remaining[child] == 0) {
                        ready.push(child);
                        report.tasks[child].state = TaskState::READY;
                    }
                }
            }
            complete_if_done();
        }
        cv.notify_all();
    }
}
