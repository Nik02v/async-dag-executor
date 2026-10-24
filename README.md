# Async DAG Executor

A C++17 project for practicing threads, condition variables, promise/future,
and DAGs. The graph is built in C++ code. The executor has a fixed number of
threads and accepts one graph during its lifetime. There is no networking,
retry mechanism, coroutine support, or result passing through the API.

## Build and run

Windows, a C++17 compiler, and CMake 3.20 or newer.
With MinGW, `g++`, `mingw32-make`, and `cmake` must be in PATH:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
.\build\dag_demo.exe
```

The MinGW bundled with CLion was tested on this machine. The MSYS2 version in
PATH compiles the code, but its linker crashes (`ld returned 116 exit status`).
For the tested toolchain, run the following from this directory in PowerShell:

```powershell
$toolRoot = Join-Path $env:LOCALAPPDATA 'Programs\CLion\bin'
$env:PATH = "$toolRoot\mingw\bin;$env:PATH"
& "$toolRoot\cmake\win\x64\bin\cmake.exe" -S . -B build-clion -G "MinGW Makefiles" "-DCMAKE_CXX_COMPILER=$toolRoot\mingw\bin\g++.exe" "-DCMAKE_MAKE_PROGRAM=$toolRoot\mingw\bin\mingw32-make.exe" -DCMAKE_BUILD_TYPE=Debug
& "$toolRoot\cmake\win\x64\bin\cmake.exe" --build build-clion -j 2
& "$toolRoot\cmake\win\x64\bin\ctest.exe" --test-dir build-clion --output-on-failure
.\build-clion\dag_demo.exe
```

You can also build directly with MinGW from PowerShell, without CMake:

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc src/task_graph.cpp src/executor.cpp src/main.cpp -o dag_demo.exe
g++ -std=c++17 -Wall -Wextra -Wpedantic -Isrc src/task_graph.cpp src/executor.cpp tests/tests.cpp -o dag_tests.exe
.\dag_tests.exe
.\dag_demo.exe
```

There are no additional libraries. Tests use a small runner with CHECK assertions
instead of an external framework; checks remain active in Release builds.

## Files

- `src/task_graph.h/.cpp`: task functions, edges, and Kahn's algorithm.
- `src/executor.h/.cpp`: pool, ready task IDs, states, and the final report.
- `src/main.cpp`: two arrays, sums of squares, merging, and reporting.
- `tests/tests.cpp`: valid and invalid graphs, parallel execution, and lifetime.

## Usage

```cpp
int a = 0, b = 0, sum = 0;
TaskGraph graph;
auto first = graph.add_task([&] { a = 12; });
auto second = graph.add_task([&] { b = 30; });
auto merge = graph.add_task([&] { sum = a + b; });
graph.add_dependency(first, merge);
graph.add_dependency(second, merge);

Executor executor(2);
auto future = executor.run(std::move(graph));
RunReport report = future.get();
```

Include `executor.h` and `<utility>` for this example. The main thread can do
other work before calling `get()`. Check `report.tasks[merge].state` before using
`sum`. The index in `report.tasks` is the ID returned by `add_task`. Each result
contains `state` and `error` (an exception_ptr). You can rethrow an error using
`std::rethrow_exception`.

Empty functions, unknown IDs, duplicate edges, and self-dependencies are rejected
while building the graph. `run` checks for cycles synchronously before accepting
the graph. An invalid graph runs none of its tasks and does not consume the
allowed run. After accepting one graph, a second `run` throws `logic_error`, even
if the first graph is empty or already finished. An empty graph immediately
returns a ready future.

## Synchronization

One mutex protects the ready queue, states, remaining predecessor counters,
finished node count, and stopping/started flags. A worker takes an ID and sets
RUNNING while holding the lock. It then releases the lock and calls the task
function. Afterward, it reacquires the lock and processes the outcome. The graph
structure does not change during execution.

Only a successful predecessor decrements a child's counter. The worker that
reduces it to zero queues the child and sets READY, under the same lock. Two
simultaneous completions cannot schedule the same node twice. Locking also
ensures that a successor sees the data written by its predecessors.

A task that throws becomes FAILED, and its descendants become SKIPPED. Traversal
is iterative; a node is marked when added to the stack, so shared descendants do
not increase the finished count multiple times. Independent branches continue.
SKIPPED has no exception of its own, while FAILED retains the original exception,
including exceptions that are not derived from std::exception.

When the finished count reaches the node count, the promise receives the report
and the pool stops. The destructor joins all threads. If a graph is running, it
waits for completion; an empty ready queue is not a reason to stop, since an
active task may later release a child. An executor whose run was never called
also stops its threads cleanly.

## Requirements for user code

Each function must return or throw. If it blocks forever, both the final future
and the destructor remain blocked. A task must not wait for its own graph's
future or destroy its executor. Call the destructor from an external thread,
with no concurrent API calls on that object.

References captured by lambdas must remain valid until execution finishes.
Independent tasks that modify the same object must use their own mutex or atomic
operations. In the demo, each parallel task has its own sum, and the merge depends
on both. The main thread reads the results after `future.get()`. No additional
lock is needed there.

## Tests

Tests cover a chain, a diamond, disconnected components, validation, an empty
graph, and rejection of a second run. Parallel execution is checked using a
condition variable: both tasks must enter their functions before the main thread
allows them to finish. The future must remain unready during that time.

Other cases include 50 coordinated completions of four predecessors sharing a
child, two failed branches with shared descendants, an independent successful
branch, destruction during execution, a layered graph with 1/2/4/8 threads, and
skipping a chain of 10,000 nodes. Waits have timeouts; the runner also has a
20-second watchdog for possible deadlocks.

Local verification results are recorded in `TESTING.md`.

The demo prints the sum **385** and SUCCEEDED for all four tasks. Execution order
between independent tasks is not guaranteed.
