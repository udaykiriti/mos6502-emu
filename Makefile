CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

.PHONY: all test clean run

all: emu

emu: main.cpp cpu6502.h
	$(CXX) $(CXXFLAGS) main.cpp -o emu

test: test_cpu6502.cpp cpu6502.h doctest.h
	$(CXX) $(CXXFLAGS) -I. test_cpu6502.cpp -o test_cpu6502
	./test_cpu6502

run: emu
	./emu

clean:
	rm -f emu test_cpu6502