SHELL = bash
CXX = $(shell echo $${CXX:-g++}) -std=c++$(shell echo $${ISOCPP:-26}) -Iinclude

DEBUG = $(shell if (: $${NDEBUG:?}) 2> /dev/null; then :; else echo 1; fi)
CXXDEBUG = -O0 -ggdb -g3  \
           -fvar-tracking -gcolumn-info -femit-class-debug-always  \
           -gstatement-frontiers -fno-eliminate-unused-debug-types  \
           -fno-merge-debug-strings -ginline-points -gdescribe-dies  \
           -fno-eliminate-unused-debug-symbols -fno-omit-frame-pointer  \
           -ftrapv -fsanitize=undefined
CXXDIAGNO = -fdiagnostics-path-format=inline-events  \
            -fconcepts-diagnostics-depth=99  \
            $(intcmp $(shell  \
                     $(CXX) -v |& grep '^gcc version' - | awk -F' ' '{printf $$3}' | awk -F. '{print $$1}'  \
              ), 13,  \
               , , -fdiagnostics-all-candidates)  \
            $(if  \
              $(shell if ((`$(CXX) -v |& grep '^gcc version' - | awk -F' ' '{printf $$3}' | awk -F. '{print $$1}'`>=14)); then echo 1; fi),  \
              -fdiagnostics-all-candidates)
CXXFLAGS = -Wpedantic -Wall -W  \
           $(if $(DEBUG), $(CXXDIAGNO))  \
           $(if $(DEBUG), $(CXXDEBUG), -g0 -O3 -D'NDEBUG')

LIBS = $(if $(shell  \
            echo $$'%:if __has_include(<format>)\n%:else\n%:if __has_include(<experimental/format>)\n%:else\n"cannot find <format>";\n%:endif\n%:endif'  \
            | $(CXX) -x c++ -E - | grep 'cannot find <format>' -  \
         ),fmt)
LIBDIRS = $(if $(LIBS),./lib/$(LIBS)-build/)
LIBFLAGS = $(if $(LIBS), -L$(LIBDIRS))
LDFLAGS = -pthread -lrt $(if $(LIBS), -l$(LIBS))

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


bin/test.exe:  src/test.cpp  include/tester.hpp  include/ipcator.hpp  $(LIBDIRS)
	mkdir -p bin
	mkdir -p /tmp/shynur/ipcator/;  \
	if time  \
	  $(CXX) -fdiagnostics-color=always $(CXXFLAGS) $< $(LIBFLAGS) $(LDFLAGS) -o $@  \
		2> /tmp/shynur/ipcator/Makefile.stderr; then  \
		:;  \
	else  \
		LASTEXITCODE=$$?;  \
		cat /tmp/shynur/ipcator/Makefile.stderr  \
		| sed -e 's/warning:/😎👌:/g' -e 's/error:/😭👊:/g';  \
		(exit $$LASTEXITCODE);  \
	fi

bin/ipc-%.exe:  src/ipc-%.cpp  include/ipcator.hpp  $(LIBDIRS)
	mkdir -p bin
	time $(CXX) $(CXXFLAGS) $< $(LIBFLAGS) $(LDFLAGS) -o $@


lib/fmt-build/:  lib/fmt-build/libfmt.a
lib/fmt-build/libfmt.a:
	mkdir -p lib/fmt-build
	cd lib/fmt-build;  \
	CXX='$(CXX)' cmake ../fmt;  \
	make -j$$[1+`nproc`]

# ----------------------------------------------------------

.PHONY: git
git:
	git commit -av
	git push

.PHONY: clean
clean:
	rm -rf bin/
	rm -f /dev/shm/*ipcator-?*
	@# 默认保留依赖库的二进制文件, 因为生成一次耗时太久.
	for lib in $(LIBS); do  \
	  mv lib/$$lib-build/lib$$lib.a lib/;  \
	  rm -rf lib/$$lib-build/*;  \
	  mv lib/lib$$lib.a lib/$$lib-build/;  \
	done

# 查看一些 Makefile 中定义的变量.
.PHONY: print-vars
print-vars:
	@echo CXX = $(CXX)
	@echo DEBUG = $(DEBUG)
	@echo CXXFLAGS = $(CXXFLAGS)
	@echo LIBS = $(LIBS)
	@echo LIBDIRS = $(LIBDIRS)
	@echo LIBFLAGS = $(LIBFLAGS)
	@echo LDFLAGS = $(LDFLAGS)
