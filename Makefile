SHELL = bash
CXX = $(shell echo $${CXX:-g++}) -std=c++$(shell echo $${ISOCPP:-26}) -Iinclude
DEBUG = $(shell if (: $${NDEBUG:?}); then :; else echo 1; fi)
CXXDEBUG = -O0 -ggdb -g3  \
           -fvar-tracking -gcolumn-info -femit-class-debug-always  \
           -gstatement-frontiers -fno-eliminate-unused-debug-types  \
           -fno-merge-debug-strings -ginline-points -gdescribe-dies  \
           -fno-eliminate-unused-debug-symbols -fno-omit-frame-pointer  \
					 -ftrapv -fsanitize=undefined
CXXDIAGNO = -fdiagnostics-path-format=inline-events  \
            -fconcepts-diagnostics-depth=99  \
            -fdiagnostics-all-candidates
CXXFLAGS = -Wpedantic -Wall -W  \
					 $(if $(DEBUG), $(CXXDEBUG), -g0 -Ofast -D'NDEBUG')  \
					 $(if $(DEBUG), $(CXXDIAGNO))
LDFLAGS = -lrt -pthread

# ----------------------------------------------------------

.PHONY: test
test:  bin/test.exe
	rm -f /dev/shm/*ipcator-?*
	@time $<

.PHONY: ipc
ipc:  bin/ipc-writer.exe  bin/ipc-reader.exe
	rm -f /dev/shm/*ipcator-?*
	echo
	@for exe in $^; do (./$$exe; echo) & done
	@wait


bin/test.exe:  src/test.cpp  include/tester.hpp  include/ipcator.hpp
	mkdir -p bin
	mkdir -p /tmp/shynur/ipcator/;  \
	if time  \
	  $(CXX) -fdiagnostics-color=always $(CXXFLAGS) $(LDFLAGS) -o $@  $<  \
    2> /tmp/shynur/ipcator/Makefile.stderr; then  \
		:;  \
	else  \
		LASTEXITCODE=$$?;  \
		cat /tmp/shynur/ipcator/Makefile.stderr  \
		| sed -e 's/warning:/😎👌:/g' -e 's/error:/😭👊:/g';  \
		(exit $$LASTEXITCODE);  \
	fi

bin/ipc-%.exe:  src/ipc-%.cpp  include/ipcator.hpp
	mkdir -p bin
	time $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@  $<

# ----------------------------------------------------------

.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
	rm -f /dev/shm/*ipcator-?*
