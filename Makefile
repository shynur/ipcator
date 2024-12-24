SHELL = bash
CXX = $(shell echo $${CXX:-g++}) -fdiagnostics-color=always -std=c++26
CXXDEBUG = -ggdb3  \
           -fvar-tracking -gcolumn-info -femit-class-debug-always  \
           -gstatement-frontiers -fno-eliminate-unused-debug-types  \
           -fno-merge-debug-strings -ginline-points -gdescribe-dies  \
           -fno-eliminate-unused-debug-symbols
CXXFLAGS = -O0 -fno-omit-frame-pointer  \
           $(CXXDEBUG)  \
           -Wpedantic -Wall -W -fconcepts-diagnostics-depth=9 -fdiagnostics-all-candidates  \
           -Iinclude
LDFLAGS = -lrt


.PHONY: run
run:  bin/debug.exe
	rm -f /dev/shm/github_dot_com_slash_shynur_slash_ipcator-?*
	@time $<
	echo $$?

.PHONY: run-build
run-build:  bin/release.exe
	rm -f /dev/shm/github_dot_com_slash_shynur_slash_ipcator-?*
	@time $<
	echo $$?

# Usage:  echo 20 | make try-backport
.PHONY: try-backport
try-backport:
	read  &&  \
	$(CXX) -std=c++$$REPLY -fpermissive -fconcepts -w -O0 -g0 -Iinclude $(LDFLAGS)  \
	  -o bin/backport-$$REPLY.exe -D'NDEBUG'  src/main.cpp

bin/debug.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	mkdir -p /tmp/shynur/ipcator/;  \
	if time $(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@  $<  \
    2> /tmp/shynur/ipcator/Makefile.stderr; then  \
		:;  \
	else  \
		LASTEXITCODE=$$?;  \
		cat /tmp/shynur/ipcator/Makefile.stderr  \
		| sed -e 's/warning/🤣👉/g' -e 's/error/🤡/g';  \
		(exit $$LASTEXITCODE);  \
	fi
	@echo '************************** 编译完成 **************************'

bin/release.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	time $(CXX) -g0 -Ofast -w -Iinclude $(LDFLAGS) -o $@ -D'NDEBUG'  $<
	@echo '************************** 编译完成 **************************'


.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/

# # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
.PHONY: ipc
ipc: bin/writer.exe bin/reader.exe
	rm -f /dev/shm/{ipcator-ipc-test,github_dot_com_slash_shynur_slash_ipcator}*
	@echo
	@for exe in $^; do $$exe & done
	@wait
	@sleep 1
	@echo

bin/writer.exe: src/writer.cpp  include/ipcator.hpp
	@mkdir -p bin
	time $(CXX) -g0 -Ofast -w -Iinclude $(LDFLAGS) -o $@ -D'NDEBUG'  $<

bin/reader.exe: src/reader.cpp  include/ipcator.hpp
	@mkdir -p bin
	time $(CXX) -g0 -Ofast -w -Iinclude $(LDFLAGS) -o $@ -D'NDEBUG'  $<
