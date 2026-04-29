cmake_minimum_required(VERSION 3.10)
project(HFT_Phase4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -O3 -march=native)

include_directories(include)

add_executable(hft_app
    src/main.cpp
    src/MarketData.cpp
)

add_executable(test_latency
    test/test_latency.cpp
    src/MarketData.cpp
)
