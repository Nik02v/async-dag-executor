#pragma once

#include <cstddef>
#include <functional>
#include <vector>

using TaskId = std::size_t;

class TaskGraph {
public:
    TaskId add_task(std::function<void()> work);
    void add_dependency(TaskId before, TaskId after);
    void validate() const;
    std::size_t size() const { return nodes.size(); }

private:
    struct Node {
        std::function<void()> work;
        std::vector<TaskId> next;
    };

    std::vector<Node> nodes;
    friend class Executor;
};
