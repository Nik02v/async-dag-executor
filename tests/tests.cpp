#include "executor.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace std::chrono_literals;

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error( \
    std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #expr); } while (false)

template<class E, class F>
void expect_throw(F f)
{
    bool caught = false;
    try {
        f();
    } catch (const E&) {
        caught = true;
    }
    CHECK(caught);
}

RunReport get_report(std::future<RunReport>& future)
{
    CHECK(future.wait_for(4s) == std::future_status::ready);
    return future.get();
}

void all_succeeded(const RunReport& report, std::size_t count)
{
    CHECK(report.tasks.size() == count);
    for (const auto& task : report.tasks) {
        CHECK(task.state == TaskState::SUCCEEDED);
        CHECK(!task.error);
    }
}

void empty_and_unused()
{
    expect_throw<std::invalid_argument>([] { Executor e(0); });
    { Executor unused(3); }
    Executor e(2);
    auto future = e.run(TaskGraph{});
    CHECK(future.wait_for(0s) == std::future_status::ready);
    CHECK(future.get().tasks.empty());
    expect_throw<std::logic_error>([&] { e.run(TaskGraph{}); });
}

void validation()
{
    TaskGraph graph;
    expect_throw<std::invalid_argument>([&] { graph.add_task({}); });
    CHECK(graph.size() == 0);
    auto a = graph.add_task([] {});
    auto b = graph.add_task([] {});
    CHECK(a == 0 && b == 1);
    expect_throw<std::out_of_range>([&] { graph.add_dependency(a, 9); });
    expect_throw<std::out_of_range>([&] { graph.add_dependency(9, b); });
    expect_throw<std::invalid_argument>([&] { graph.add_dependency(a, a); });
    graph.add_dependency(a, b);
    expect_throw<std::invalid_argument>([&] { graph.add_dependency(a, b); });
    graph.validate();

    std::atomic<int> calls{0};
    TaskGraph cycle;
    cycle.add_task([&] { ++calls; });
    auto x = cycle.add_task([&] { ++calls; });
    auto y = cycle.add_task([&] { ++calls; });
    cycle.add_dependency(x, y);
    cycle.add_dependency(y, x);
    Executor e(4);
    expect_throw<std::invalid_argument>([&] { e.run(cycle); });
    CHECK(calls == 0);

    auto future = e.run(graph);
    all_succeeded(get_report(future), 2);
    expect_throw<std::logic_error>([&] { e.run(graph); });
}

void chain_and_components()
{
    for (int threads : {1, 2, 4}) {
        int value = 0;
        std::array<int, 4> calls{};
        TaskGraph g;
        auto a = g.add_task([&] { CHECK(value == 0); value = 10; ++calls[0]; });
        auto b = g.add_task([&] { CHECK(value == 10); value += 20; ++calls[1]; });
        auto c = g.add_task([&] { CHECK(value == 30); value *= 2; ++calls[2]; });
        g.add_task([&] { ++calls[3]; });
        g.add_dependency(a, b);
        g.add_dependency(b, c);
        Executor e(threads);
        auto future = e.run(std::move(g));
        all_succeeded(get_report(future), 4);
        CHECK(value == 60);
        for (int n : calls)
            CHECK(n == 1);
    }
}

void diamond()
{
    for (int threads : {1, 2, 4}) {
        int input = 0, left = 0, right = 0, answer = 0;
        std::array<int, 4> calls{};
        TaskGraph g;
        auto a = g.add_task([&] { input = 7; ++calls[0]; });
        auto b = g.add_task([&] { left = input * 2; ++calls[1]; });
        auto c = g.add_task([&] { right = input * 3; ++calls[2]; });
        auto d = g.add_task([&] { answer = left + right; ++calls[3]; });
        g.add_dependency(a, b);
        g.add_dependency(a, c);
        g.add_dependency(b, d);
        g.add_dependency(c, d);
        Executor e(threads);
        auto future = e.run(g);
        all_succeeded(get_report(future), 4);
        CHECK(answer == 35);
        for (int count : calls)
            CHECK(count == 1);
    }
}

void parallel_and_async()
{
    std::mutex m;
    std::condition_variable cv;
    int entered = 0;
    bool release = false;
    auto work = [&] {
        std::unique_lock<std::mutex> lock(m);
        ++entered;
        cv.notify_all();
        CHECK(cv.wait_for(lock, 2s, [&] { return release; }));
    };
    TaskGraph g;
    g.add_task(work);
    g.add_task(work);
    Executor e(2);
    auto future = e.run(g);
    bool both;
    bool not_ready;
    {
        std::unique_lock<std::mutex> lock(m);
        both = cv.wait_for(lock, 2s, [&] { return entered == 2; });
        not_ready = future.wait_for(0s) == std::future_status::timeout;
        release = true;
    }
    cv.notify_all();
    auto report = get_report(future);
    CHECK(both);
    CHECK(not_ready);
    all_succeeded(report, 2);
}

void simultaneous_parents()
{
    for (int repeat = 0; repeat < 50; ++repeat) {
        std::mutex m;
        std::condition_variable cv;
        int entered = 0;
        bool release = false;
        std::array<int, 4> values{};
        std::atomic<int> child_calls{0};
        TaskGraph g;
        for (int i = 0; i < 4; ++i) {
            g.add_task([&, i] {
                std::unique_lock<std::mutex> lock(m);
                ++entered;
                cv.notify_all();
                CHECK(cv.wait_for(lock, 2s, [&] { return release; }));
                lock.unlock();
                values[i] = i + 1;
            });
        }
        auto child = g.add_task([&] {
            ++child_calls;
            CHECK(values[0] + values[1] + values[2] + values[3] == 10);
        });
        for (TaskId i = 0; i < 4; ++i)
            g.add_dependency(i, child);

        Executor e(4);
        auto future = e.run(std::move(g));
        bool all_entered;
        {
            std::unique_lock<std::mutex> lock(m);
            all_entered = cv.wait_for(lock, 2s, [&] { return entered == 4; });
            release = true;
        }
        cv.notify_all();
        auto report = get_report(future);
        CHECK(all_entered);
        all_succeeded(report, 5);
        CHECK(child_calls == 1);
    }
}

void failure_and_shared_descendants()
{
    for (int threads : {1, 2, 4}) {
        std::atomic<int> skipped_calls{0};
        int independent = 0;
        TaskGraph g;
        auto a = g.add_task([] { throw std::runtime_error("first error"); });
        auto b = g.add_task([] { throw 42; });
        auto left = g.add_task([&] { ++skipped_calls; });
        auto right = g.add_task([&] { ++skipped_calls; });
        auto join = g.add_task([&] { ++skipped_calls; });
        auto tail = g.add_task([&] { ++skipped_calls; });
        auto ok = g.add_task([&] { independent = 11; });
        auto ok2 = g.add_task([&] { independent *= 2; });
        g.add_dependency(a, left);
        g.add_dependency(b, right);
        g.add_dependency(left, join);
        g.add_dependency(right, join);
        g.add_dependency(join, tail);
        g.add_dependency(ok, ok2);

        g.add_dependency(ok2, tail);
        Executor e(threads);
        auto future = e.run(std::move(g));
        auto r = get_report(future);
        CHECK(r.tasks.size() == 8);
        CHECK(r.tasks[a].state == TaskState::FAILED);
        CHECK(r.tasks[b].state == TaskState::FAILED);
        CHECK(r.tasks[a].error && r.tasks[b].error);
        try {
            std::rethrow_exception(r.tasks[a].error);
        } catch (const std::runtime_error& err) {
            CHECK(std::string(err.what()) == "first error");
        }
        try {
            std::rethrow_exception(r.tasks[b].error);
        } catch (int n) {
            CHECK(n == 42);
        }
        for (TaskId id : {left, right, join, tail}) {
            CHECK(r.tasks[id].state == TaskState::SKIPPED);
            CHECK(!r.tasks[id].error);
        }
        CHECK(skipped_calls == 0);
        CHECK(independent == 22);
        CHECK(r.tasks[ok].state == TaskState::SUCCEEDED);
        CHECK(r.tasks[ok2].state == TaskState::SUCCEEDED);
    }
}

void failure_while_other_parent_runs()
{
    std::mutex m;
    std::condition_variable cv;
    bool entered = false, release = false;
    std::atomic<int> child_calls{0};
    TaskGraph g;
    auto slow = g.add_task([&] {
        std::unique_lock<std::mutex> lock(m);
        entered = true;
        cv.notify_all();
        CHECK(cv.wait_for(lock, 2s, [&] { return release; }));
    });
    auto fail = g.add_task([] { throw std::runtime_error("fail"); });
    auto child = g.add_task([&] { ++child_calls; });
    g.add_dependency(slow, child);
    g.add_dependency(fail, child);
    Executor e(2);
    auto future = e.run(g);
    bool waiting;
    bool unfinished;
    {
        std::unique_lock<std::mutex> lock(m);
        waiting = cv.wait_for(lock, 2s, [&] { return entered; });
        unfinished = future.wait_for(0s) == std::future_status::timeout;
        release = true;
    }
    cv.notify_all();
    auto r = get_report(future);
    CHECK(waiting && unfinished);
    CHECK(r.tasks[slow].state == TaskState::SUCCEEDED);
    CHECK(r.tasks[fail].state == TaskState::FAILED);
    CHECK(r.tasks[child].state == TaskState::SKIPPED);
    CHECK(child_calls == 0);
}

void destructor_waits()
{
    std::mutex m;
    std::condition_variable cv;
    bool entered = false, release = false;
    std::atomic<int> finished{0};
    std::promise<void> destroying;
    auto destroying_future = destroying.get_future();
    std::promise<void> destroyed;
    auto destroyed_future = destroyed.get_future();
    std::future<RunReport> result;

    std::thread owner([&] {
        {
            Executor e(2);
            TaskGraph g;
            auto a = g.add_task([&] {
                std::unique_lock<std::mutex> lock(m);
                entered = true;
                cv.notify_all();
                CHECK(cv.wait_for(lock, 2s, [&] { return release; }));
                ++finished;
            });
            auto b = g.add_task([&] { ++finished; });
            g.add_dependency(a, b);
            result = e.run(std::move(g));
            destroying.set_value();
        }
        destroyed.set_value();
    });

    bool began = destroying_future.wait_for(2s) == std::future_status::ready;
    bool active;
    bool held;
    {
        std::unique_lock<std::mutex> lock(m);
        active = cv.wait_for(lock, 2s, [&] { return entered; });
        held = destroyed_future.wait_for(50ms) == std::future_status::timeout;
        release = true;
    }
    cv.notify_all();
    owner.join();
    CHECK(began && active && held);
    CHECK(finished == 2);
    all_succeeded(get_report(result), 2);
}

void layered_graph()
{
    constexpr int width = 8;
    constexpr int count = 128;
    for (int threads : {1, 2, 4, 8}) {
        std::array<std::atomic<int>, count> calls{};

        for (auto& n : calls)
            n.store(0);
        TaskGraph g;
        for (int i = 0; i < count; ++i) {
            g.add_task([&, i] {
                CHECK(calls[i].fetch_add(1) == 0);
                if (i >= width) {
                    int previous_layer = (i / width - 1) * width;
                    for (int j = previous_layer; j < previous_layer + width; ++j)
                        CHECK(calls[j] == 1);
                }
            });
        }
        for (TaskId i = width; i < count; ++i) {
            TaskId previous_layer = (i / width - 1) * width;
            for (TaskId j = previous_layer; j < previous_layer + width; ++j)
                g.add_dependency(j, i);
        }
        Executor e(threads);
        auto future = e.run(std::move(g));
        all_succeeded(get_report(future), count);
        for (auto& n : calls)
            CHECK(n == 1);
    }
}

void deep_failure()
{

    TaskGraph g;
    g.add_task([] { throw std::runtime_error("root failed"); });
    std::atomic<int> calls{0};
    for (TaskId i = 1; i < 10000; ++i) {
        g.add_task([&] { ++calls; });
        g.add_dependency(i - 1, i);
    }
    Executor e(4);
    auto future = e.run(std::move(g));
    auto r = get_report(future);
    CHECK(r.tasks.size() == 10000);
    CHECK(r.tasks[0].state == TaskState::FAILED);
    for (TaskId i = 1; i < r.tasks.size(); ++i)
        CHECK(r.tasks[i].state == TaskState::SKIPPED);
    CHECK(calls == 0);
}

int main()
{

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(m);
        if (!cv.wait_for(lock, 20s, [&] { return done; })) {
            std::cerr << "Tests exceeded 20 seconds (possible deadlock)\n";
            std::abort();
        }
    });

    const std::pair<const char*, void(*)()> tests[] = {
        {"empty and unused executor", empty_and_unused},
        {"graph validation and second run", validation},
        {"chain and disconnected components", chain_and_components},
        {"diamond", diamond},
        {"parallel work and async return", parallel_and_async},
        {"simultaneous parents, one child", simultaneous_parents},
        {"failure and overlapping descendants", failure_and_shared_descendants},
        {"failure with running parent", failure_while_other_parent_runs},
        {"destructor waits for graph", destructor_waits},
        {"layered graph", layered_graph},
        {"deep failure propagation", deep_failure}
    };

    int failed = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[OK] " << test.first << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": " << e.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.first << ": unknown exception\n";
        }
    }
    {
        std::lock_guard<std::mutex> lock(m);
        done = true;
    }
    cv.notify_all();
    watchdog.join();
    std::cout << "Failed tests: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
