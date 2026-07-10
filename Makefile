CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -I src
SRC=src/lexer.cpp src/parser.cpp src/value.cpp src/interpreter.cpp src/main.cpp
TARGET=cotton

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) -lm

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./$(TARGET) examples/closest_pair.cot
	./$(TARGET) examples/comprehension.cot
	./$(TARGET) examples/match.cot
	./$(TARGET) examples/error_handling.cot
	./$(TARGET) examples/ownership.cot

clean:
	rm -f $(TARGET)
	rm -rf build
