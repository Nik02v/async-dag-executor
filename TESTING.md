# Local verification

Environment: Windows, CLion MinGW GCC 13.1.0, C++17, CMake Debug build.
Commands for this toolchain are in the README. The successful build is in
`build-clion`.

`cmake --build build-clion -j 2` succeeds, and CTest reports:

```text
1/1 Test #1: dag_tests ... Passed
100% tests passed, 0 tests failed out of 1
```

One CTest executable contains 11 test groups:

- empty graph, zero threads, and an executor without a run;
- graph validation, a cycle before execution, and a second run;
- chain and disconnected components;
- diamond;
- coordinated parallel execution and asynchronous return;
- four predecessors sharing one child, repeated 50 times;
- exceptions, shared skipped descendants, and an independent branch;
- a failed predecessor while another task is running;
- destruction during an active graph;
- a layered graph with 128 nodes and 1, 2, 4, and 8 threads;
- failure propagation through a chain of 10,000 nodes.

The demo returns exit code 0, the sum 385, and SUCCEEDED for all four tasks.

MSYS2 GCC 14.2.0 compiled the sources with `-Wall -Wextra -Wpedantic -Werror`,
but its linker failed with `ld returned 116 exit status`. The final build and
test execution were therefore checked using CLion's toolchain.

## English text update verification

After removing comments and translating project text, the Debug build succeeded.
CTest passed all 11 test groups. The demo returned exit code 0, printed the sum
385, and reported SUCCEEDED for all four tasks.
Source comparison confirmed that code structure was unchanged apart from
removing comments and translating string literals.
