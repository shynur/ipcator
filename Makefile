SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23  \
           -O0 -fno-omit-frame-pointer  \
           -ggdb3 -fvar-tracking -gcolumn-info -femit-class-debug-always  \
                  -gstatement-frontiers -fno-eliminate-unused-debug-types  \
                  -fno-merge-debug-strings -ginline-points -gdescribe-dies  \
                  -fno-eliminate-unused-debug-symbols  \
           -Wpedantic -Wall -W  \
           -Iinclude
LDFLAGS = -lrt

.PHONY: run
run:  bin/debug.exe
	@time $<
	echo $$?

.PHONY: run-build
run-build:  bin/release.exe
	@time $<
	echo $$?

bin/debug.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(AggressiveOptimization) $(LDFLAGS) -o $@  $<

bin/release.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	time $(CXX) $(CXXFLAGS) -g0 -Ofast $(LDFLAGS) -o $@ -D'NDEBUG'  $<

.PHONY: git
git:
	git add .
	git commit -v
	git push

.PHONY: clean
clean:
	rm -rf bin/
