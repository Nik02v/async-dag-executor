cmake_minimum_required(VERSION 3.20)
project(async_dag_executor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(dag src/task_graph.cpp src/executor.cpp)
target_include_directories(dag PUBLIC src)
target_link_libraries(dag PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(dag PRIVATE /W4)
else()
    target_compile_options(dag PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(dag_demo src/main.cpp)
target_link_libraries(dag_demo PRIVATE dag)

enable_testing()
add_executable(dag_tests tests/tests.cpp)
target_link_libraries(dag_tests PRIVATE dag)
add_test(NAME dag_tests COMMAND dag_tests)
set_tests_properties(dag_tests PROPERTIES TIMEOUT 30)
