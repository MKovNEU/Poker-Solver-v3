CXX = g++

# Added -I./lib/isomorphism here too so CXX can find the headers
CXXFLAGS = -std=c++17 -O3 -march=native -flto -ffast-math -DNDEBUG -Wall -Wextra -I. -I./include

# Replace your current flags with these temporarily
#CFLAGS = -g -fsanitize=address -I./lib/isomorphism
#CXXFLAGS = -std=c++17 -g -fsanitize=address -I. -I./include

# Removed check-main.c
SRCS = src/main.cpp src/solver/cfr.cpp src/solver/ranges.cpp src/solver/tree_builder.cpp src/solver/isomorpher.cpp src/game/hand_eval.cpp src/game/state.cpp 

OBJS = $(SRCS:.cpp=.o)

TARGET = solver

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)