SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra -O0 -ggdb -g3  \
           -Iinclude
LDFLAGS = -lrt

.PHONY: test
test:  bin/a.exe
	time bin/a.exe
	echo $$?

bin/a.exe:  include/ipcator.hpp  src/a.cpp
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o bin/a.exe  src/a.cpp

.PHONY: git
git:
	git add .
	git commit -v
	git push
