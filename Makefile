SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -O0 -ggdb -g3  \
           -Iinclude
LDFLAGS = -lrt

.PHONY: test
test:  bin/a.exe
	time bin/a.exe

bin/a.exe:  include/ipcator.hpp  src/a.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o bin/a.exe  src/a.cpp
