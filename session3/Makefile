CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g

BINS = part1 part2 part3 part4

all: $(BINS)

part1: part1_warmup.cpp
	$(CXX) $(CXXFLAGS) -o part1 part1_warmup.cpp

part2: part2_fixed.cpp
	$(CXX) $(CXXFLAGS) -o part2 part2_fixed.cpp

part3: part3_trade_handle.cpp
	$(CXX) $(CXXFLAGS) -o part3 part3_trade_handle.cpp

part4: part4_shared_handle.cpp
	$(CXX) $(CXXFLAGS) -o part4 part4_shared_handle.cpp

run: all
	@echo "=== part1 ===" && ./part1
	@echo "=== part2 ===" && ./part2
	@echo "=== part3 ===" && ./part3
	@echo "=== part4 ===" && ./part4

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

.PHONY: all run clean
