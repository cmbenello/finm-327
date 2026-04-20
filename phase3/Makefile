CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g
ASAN = -fsanitize=address -fno-omit-frame-pointer

SRC = main.cpp market_snapshot.cpp order_manager.cpp
HDR = market_snapshot.h order_manager.h feed_parser.h

BIN = trader
BIN_ASAN = trader_asan

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

asan: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(ASAN) -o $(BIN_ASAN) $(SRC)

run: $(BIN)
	./$(BIN) sample_feed.txt | tee output.log

run-asan: asan
	./$(BIN_ASAN) sample_feed.txt

clean:
	rm -f $(BIN) $(BIN_ASAN) output.log
	rm -rf *.dSYM

.PHONY: all asan run run-asan clean
