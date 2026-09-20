SHELL = bash

CXXFLAGS := -Iinclude

LIBS := $(if $(shell  \
             echo $$'%:if __has_include(<format>)\n%:elif __has_include(<experimental/format>)\n%:else\n"cannot find <format>";\n%:endif\n'  \
             | $(CXX) -x c++ -E - | grep 'cannot find <format>' -  \
          ),fmt)
LIBARS := $(LIBS:%=lib/archives/lib%.a)
LDFLAGS := -pthread -lrt $(if $(LIBS), -l$(LIBS))

.PHONY: test
test:  bin/test.exe
	rm -f /dev/shm/ipcator.*
	@time $<

.PHONY: ipc
ipc:  bin/ipc-writer.exe  bin/ipc-reader.exe
	rm -f /dev/shm/ipcator.*
	echo
	@for exe in $^; do (./$$exe; echo) & done; wait

bin/test.exe:  src/test.cpp  include/ipcator.hpp  $(LIBARS) | bin/
	time $(CXX) $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@

bin/ipc-%.exe:  src/ipc-%.cpp  include/ipcator.hpp  $(LIBARS) | bin/
	time $(CXX) $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@

lib/archives/libfmt.a: | lib/fmt-build/  lib/archives/
	cd lib/fmt-build;  \
	CXX='$(CXX) -g0 -O0 -w' cmake -D'FMT_TEST=false'`#不进行测试, 太浪费时间了` ../fmt;  \
	make -j$$[1+`nproc`]
	mv lib/fmt-build/libfmt.a lib/archives/

%/:
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf bin/
	rm -f  /dev/shm/ipcator.*
	rm -rf lib/?*-build/
	rm -rf lib/archives/
	rm -rf docs/html/
