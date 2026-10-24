#include "executor.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

int main()
{
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {6, 7, 8, 9, 10};
    long long sum_a = 0, sum_b = 0, total = 0;
    std::string message;

    TaskGraph graph;
    TaskId first = graph.add_task([&] {
        for (int x : a)
            sum_a += x * x;
    });
    TaskId second = graph.add_task([&] {
        for (int x : b)
            sum_b += x * x;
    });
    TaskId merge = graph.add_task([&] { total = sum_a + sum_b; });
    TaskId print = graph.add_task([&] {
        message = "Sum of squares of both arrays = " + std::to_string(total);
    });
    graph.add_dependency(first, merge);
    graph.add_dependency(second, merge);
    graph.add_dependency(merge, print);

    Executor executor(2);
    auto done = executor.run(std::move(graph));
    std::cout << "Graph submitted to the pool; the main thread can continue its work.\n";
    RunReport report = done.get();

    std::cout << message << '\n';
    for (TaskId i = 0; i < report.tasks.size(); ++i) {
        std::cout << "Task " << i << ": " << state_name(report.tasks[i].state) << '\n';
        if (report.tasks[i].error) {
            try {
                std::rethrow_exception(report.tasks[i].error);
            } catch (const std::exception& e) {
                std::cout << "  " << e.what() << '\n';
            } catch (...) {
                std::cout << "  unknown exception\n";
            }
        }
    }
    return total == 385 ? 0 : 1;
}
