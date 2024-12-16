SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23  \
           -O0 -ggdb -g3  \
           -Wpedantic -Wall -W  \
					 -Wno-dangling-else  \
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
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
