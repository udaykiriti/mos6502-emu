CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
TARGET = 6502
SRC = main_6502.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean