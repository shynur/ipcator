SHELL = bash -O globstar

CXXFLAGS := -Iinclude
LDFLAGS := -pthread -lrt

.PHONY: test
test:  bin/test.exe
	rm -f /dev/shm/ipcator.*
	@time $<

.PHONY: ipc
ipc:  bin/ipc-writer.exe  bin/ipc-reader.exe
	rm -f /dev/shm/ipcator.*
	echo
	@for exe in $^; do (./$$exe; echo) & done; wait

bin/test.exe:  src/test.cpp  include/ipcator.hpp | bin/
	time $(CXX) $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@

bin/ipc-%.exe:  src/ipc-%.cpp  include/ipcator.hpp | bin/
	time $(CXX) $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@

%/:
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf bin/
	rm -f  /dev/shm/ipcator.*
