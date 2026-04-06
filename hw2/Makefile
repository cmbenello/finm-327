CXX = clang++
CXXFLAGS_COMMON = -std=c++17 -Wall -Wextra

# default is optimized build
all: test bench

# ---- correctness tests ----
test: test_bin
	./test_bin

test_bin: test.cpp linalg.cpp linalg.h
	$(CXX) $(CXXFLAGS_COMMON) -O2 -o test_bin test.cpp linalg.cpp

# ---- benchmarks at -O3 ----
bench: bench_O3
	./bench_O3

bench_O3: benchmark.cpp linalg.cpp linalg.h
	$(CXX) $(CXXFLAGS_COMMON) -O3 -o bench_O3 benchmark.cpp linalg.cpp

# ---- benchmarks at -O0 (for inlining experiment) ----
bench-O0: bench_O0
	./bench_O0

bench_O0: benchmark.cpp linalg.cpp linalg.h
	$(CXX) $(CXXFLAGS_COMMON) -O0 -o bench_O0 benchmark.cpp linalg.cpp

# ---- profiling build (with debug symbols for Instruments) ----
profile: bench_profile
	@echo "run with: xcrun xctrace record --template 'Time Profiler' --launch ./bench_profile"
	@echo "or open Instruments.app and point it at ./bench_profile"

bench_profile: benchmark.cpp linalg.cpp linalg.h
	$(CXX) $(CXXFLAGS_COMMON) -O2 -g -o bench_profile benchmark.cpp linalg.cpp

clean:
	rm -f test_bin bench_O3 bench_O0 bench_profile
	rm -rf *.dSYM

.PHONY: all test bench bench-O0 profile clean
