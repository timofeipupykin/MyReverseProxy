cmake_minimum_required(VERSION 3.20)
project(async_proxy VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PROXY_BUILD_TESTS "Build unit tests" ON)
option(PROXY_SANITIZERS  "Enable ASan+UBSan" OFF)

# Boost.Asio - header-only, нужен Boost >= 1.80 (asio::awaitable, awaitable_operators)
find_package(Boost 1.80 REQUIRED)
find_package(Threads REQUIRED)

add_library(proxy_core STATIC
  src/http_parser.cpp
  src/load_balancer.cpp
  src/routing_table.cpp
  src/config.cpp
  src/net.cpp
  src/thread_pool.cpp
  src/health_checker.cpp
  src/proxy_server.cpp
)
target_include_directories(proxy_core PUBLIC include)
target_link_libraries(proxy_core PUBLIC Boost::headers Threads::Threads)
target_compile_definitions(proxy_core PUBLIC BOOST_ASIO_NO_DEPRECATED)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(proxy_core PUBLIC -Wall -Wextra -Wpedantic)
  if(PROXY_SANITIZERS)
    target_compile_options(proxy_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(proxy_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

add_executable(async_proxy src/main.cpp)
target_link_libraries(async_proxy PRIVATE proxy_core)

if(PROXY_BUILD_TESTS)
  enable_testing()
  add_executable(proxy_tests tests/tests.cpp)
  target_link_libraries(proxy_tests PRIVATE proxy_core)
  add_test(NAME proxy_tests COMMAND proxy_tests)
endif()
