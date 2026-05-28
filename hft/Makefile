CXX      := g++-9
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O3 -march=native -mtune=native -funroll-loops -ffast-math -fno-plt
LDFLAGS  := -pthread

SRC := src
BIN := bin
BENCH := bench

CLIENTS := client client_naive
BENCHES := micro correctness
SERVER  := server

ALL := $(addprefix $(BIN)/,$(CLIENTS) $(BENCHES) $(SERVER))

all: $(BIN) $(ALL)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/server: $(SRC)/server.cpp
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -o $@ $< $(LDFLAGS)

$(BIN)/client: $(SRC)/client.cpp $(SRC)/trace.hpp
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -o $@ $< $(LDFLAGS)

$(BIN)/client_naive: $(SRC)/client_naive.cpp
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -o $@ $< $(LDFLAGS)

$(BIN)/micro: $(BENCH)/micro.cpp $(SRC)/client.cpp $(SRC)/trace.hpp
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -DMICROBENCH -o $@ $(BENCH)/micro.cpp $(LDFLAGS)

$(BIN)/correctness: $(BENCH)/correctness.cpp $(SRC)/trace.hpp
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -o $@ $(BENCH)/correctness.cpp $(LDFLAGS)

clean:
	rm -rf $(BIN)

.PHONY: all clean
