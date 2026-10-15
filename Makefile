CXX = g++
CXXFLAGS = -std=c++20 -O3 -march=native -ffast-math -fopenmp -Wall -Wextra -I.
LDFLAGS = -fopenmp

ifeq ($(OS),Windows_NT)
EXE = .exe
endif

LIB_SRCS = src/core/cards.cpp src/core/hand_eval.cpp src/game/config.cpp src/game/tree.cpp \
           src/solver/solver.cpp src/solver/explorer.cpp src/solver/protocol.cpp \
           src/preflop/equity.cpp src/preflop/game.cpp src/preflop/solver.cpp src/preflop/explorer.cpp src/preflop/protocol.cpp
LIB_OBJS = $(LIB_SRCS:.cpp=.o)

SOLVER = solver$(EXE)
TESTS = run_tests$(EXE)

all: $(SOLVER) $(TESTS)

$(SOLVER): $(LIB_OBJS) src/main.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TESTS): $(LIB_OBJS) tests/tests.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTS)
	./$(TESTS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(LIB_OBJS:.o=.d) src/main.d tests/tests.d

clean:
	rm -f $(LIB_OBJS) $(LIB_OBJS:.o=.d) src/main.o src/main.d tests/tests.o tests/tests.d $(SOLVER) $(TESTS)

.PHONY: all test clean
