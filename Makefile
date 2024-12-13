SHELL = bash
CXX = g++
CXXFLAGS = -std=c++23  \
           -O0 -ggdb -g3  \
           -Wpedantic -Wall -W  \
           -Iinclude
LDFLAGS = -lrt

.PHONY: test
test:  bin/main.exe
	@time bin/main.exe
	echo $$?

.PHONY: release
release:
	g++ -std=c++23 -D NDEBUG -O3 -Iinclude $(LDFLAGS) -o bin/main.exe  src/main.cpp
	@time bin/main.exe
	echo $$?

bin/main.exe:  include/ipcator.hpp  include/tester.hpp  src/main.cpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o bin/main.exe  src/main.cpp

.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
