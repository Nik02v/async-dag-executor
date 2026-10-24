#include "task_graph.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <utility>

TaskId TaskGraph::add_task(std::function<void()> work)
{
    if (!work)
        throw std::invalid_argument("Empty task function");

    TaskId id = nodes.size();
    nodes.push_back({std::move(work), {}});
    return id;
}

void TaskGraph::add_dependency(TaskId before, TaskId after)
{
    if (before >= nodes.size() || after >= nodes.size())
        throw std::out_of_range("Unknown task ID");
    if (before == after)
        throw std::invalid_argument("A task cannot depend on itself");

    auto& next = nodes[before].next;
    if (std::find(next.begin(), next.end(), after) != next.end())
        throw std::invalid_argument("Duplicate edge");

    next.push_back(after);
}

void TaskGraph::validate() const
{

    std::vector<std::size_t> degree(nodes.size(), 0);
    for (const auto& node : nodes) {
        for (TaskId id : node.next)
            ++degree[id];
    }

    std::queue<TaskId> q;
    for (TaskId i = 0; i < nodes.size(); ++i) {
        if (degree[i] == 0)
            q.push(i);
    }

    std::size_t visited = 0;
    while (!q.empty()) {
        TaskId id = q.front();
        q.pop();
        ++visited;
        for (TaskId child : nodes[id].next) {
            --degree[child];
            if (degree[child] == 0)
                q.push(child);
        }
    }

    if (visited != nodes.size())
        throw std::invalid_argument("Graph contains a cycle");
}
