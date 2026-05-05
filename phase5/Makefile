CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Iinclude
DBGFLAGS ?= -std=c++17 -O0 -g -fsanitize=address,undefined -Wall -Wextra -Iinclude

SRC_BENCH := src/benchmark.cpp
SRC_DEMO  := src/main.cpp
SRC_TEST  := test/test_orderbook.cpp

.PHONY: all bench demo tests run-tests run-bench chart clean

all: bench demo tests

bench:    bench_app
demo:     demo_app
tests:    test_app

bench_app: $(SRC_BENCH) include/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_BENCH)

demo_app: $(SRC_DEMO) include/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRC_DEMO)

# tests use the asan/ubsan build by default to catch lifetime bugs in the pool.
test_app: $(SRC_TEST) include/*.hpp
	$(CXX) $(DBGFLAGS) -o $@ $(SRC_TEST)

run-tests: test_app
	./test_app

run-bench: bench_app
	./bench_app bench_results.csv

chart: bench_results.csv
	python3 plot_results.py

clean:
	rm -rf bench_app demo_app test_app *.dSYM bench_results.csv perf_chart.png
