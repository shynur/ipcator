SHELL = bash
CXX = g++ -fdiagnostics-color=always
CXXFLAGS = -std=c++26  \
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
	@echo '********** 编译完成 **********'

bin/release.exe:  src/main.cpp  include/ipcator.hpp  include/tester.hpp
	@mkdir -p bin
	time $(CXX) -std=c++26 -g0 -Ofast -w -Iinclude $(LDFLAGS) -o $@ -D'NDEBUG'  $<
	@echo '********** 编译完成 **********'

.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
