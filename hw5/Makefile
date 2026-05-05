CXX      = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -DNDEBUG -march=native

BINS = part1 part2 part3 part4

all: $(BINS)

part1: part1_hashtable.cpp
	$(CXX) $(CXXFLAGS) -o part1 part1_hashtable.cpp

part2: part2_priority_queue.cpp
	$(CXX) $(CXXFLAGS) -o part2 part2_priority_queue.cpp

part3: part3_timeseries_simd.cpp
	$(CXX) $(CXXFLAGS) -o part3 part3_timeseries_simd.cpp

part4: part4_orderbook.cpp
	$(CXX) $(CXXFLAGS) -o part4 part4_orderbook.cpp

run: all
	@echo "=== part1 ===" && ./part1
	@echo
	@echo "=== part2 ===" && ./part2
	@echo
	@echo "=== part3 ===" && ./part3
	@echo
	@echo "=== part4 ===" && ./part4

bench: all
	@./part1 > bench_results.txt
	@echo "" >> bench_results.txt
	@./part2 >> bench_results.txt
	@echo "" >> bench_results.txt
	@./part3 >> bench_results.txt
	@echo "" >> bench_results.txt
	@./part4 >> bench_results.txt
	@cat bench_results.txt

clean:
	rm -f $(BINS) bench_results.txt
	rm -rf *.dSYM

.PHONY: all run bench clean
