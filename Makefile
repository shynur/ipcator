SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23  \
           -O0 -ggdb -g3  \
           -Wpedantic -Wall -W  \
           -Iinclude
LDFLAGS = -lrt

.PHONY: run
run:  bin/debug.exe
	@time bin/debug.exe
	echo $$?

.PHONY: run-build
run-build:  bin/release.exe
	@time bin/release.exe
	echo $$?

bin/debug.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -g0 -Ofast $(AggressiveOptimization) $(LDFLAGS) -o $@ -D'NDEBUG'  $<

bin/release.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@  $<

.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
