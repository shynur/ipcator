SHELL = bash
CXX := $(shell echo $${CXX:-g++}) -std=c++$(shell echo $${ISOCPP:-26})

DEBUG != if (: $${NDEBUG:?}) 2> /dev/null; then :; else echo 1; fi
LOG != if (: $${LOG:?}) 2> /dev/null; then echo 1; fi
OFAST != if (: $${OFAST:?}) 2> /dev/null; then echo 1; fi

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
CXXFLAGS := -Iinclude  \
            -Wpedantic -Wall -W  \
            $(if $(DEBUG), $(CXXDIAGNO))  \
            $(if $(DEBUG), $(CXXDEBUG), -g0 -O3 -D'NDEBUG')  \
            $(if $(LOG), -D'IPCATOR_LOG')  \
            $(if $(OFAST), -D'IPCATOR_OFAST')

LIBS := $(if $(shell  \
             echo $$'%:if __has_include(<format>)\n%:elif __has_include(<experimental/format>)\n%:else\n"cannot find <format>";\n%:endif\n'  \
             | $(CXX) -x c++ -E - | grep 'cannot find <format>' -  \
          ),fmt)
LIBARS := $(LIBS:%=lib/archives/lib%.a)
LDFLAGS := -pthread -lrt $(if $(LIBS), -l$(LIBS))

BUILD_INFO := $(if $(LOG),with_log)-$(if $(DEBUG),,nocheck)-$(if $(OFAST),fast)-$(shell basename `echo $(CXX) | awk -F' ' '{printf $$1}'`)-C++$(shell echo $${ISOCPP:-26})

# ----------------------------------------------------------

.PHONY: test
test:  bin/test-$(BUILD_INFO).exe
	rm -f /dev/shm/*ipcator?*
	@time $<

.PHONY: ipc
ipc:  bin/ipc-writer-$(BUILD_INFO).exe  bin/ipc-reader-$(BUILD_INFO).exe
	rm -f /dev/shm/*ipcator?*
	echo
	@for exe in $^; do (./$$exe; echo) & done; wait


bin/test-$(BUILD_INFO).exe:  src/test.cpp  include/ipcator.hpp  $(LIBARS) | bin/
	mkdir -p /tmp/shynur/ipcator/;  \
	if time  \
		$(CXX) -fdiagnostics-color=always $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@  \
		2> /tmp/shynur/ipcator/Makefile-stderr.txt; then  \
		:;  \
	else  \
		LASTEXITCODE=$$?;  \
		cat /tmp/shynur/ipcator/Makefile-stderr.txt  \
		| sed -e 's/warning:/😩🙏:/g' -e 's/error:/😭👊:/g';  \
		rm /tmp/shynur/ipcator/Makefile-stderr.txt;  \
		(exit $$LASTEXITCODE);  \
	fi

bin/ipc-%-$(BUILD_INFO).exe:  src/ipc-%.cpp  include/ipcator.hpp  $(LIBARS) | bin/
	time $(CXX) $(CXXFLAGS) $< -L./lib/archives $(LDFLAGS) -o $@


lib/archives/libfmt.a: | lib/fmt-build/  lib/archives/
	cd lib/fmt-build;  \
	CXX='$(CXX) -g0 -O3' cmake -D'FMT_TEST=false' ../fmt;  \
	make -j$$[1+`nproc`]  # 不进行测试, 太浪费时间了.
	mv lib/fmt-build/libfmt.a lib/archives/


%/:
	mkdir -p $@

# ----------------------------------------------------------

.PHONY: git
git:
	git commit -av
	git push

.PHONY: doc
doc:  docs/html/index.html
	@echo $$'\033[32m文档在 $<\033[0m'
docs/html/index.html:  docs/Doxyfile.ini  include/ipcator.hpp | docs/
	cd docs;  \
	doxygen $(<F)

.PHONY: clean
clean:
	rm -rf bin/
	rm -f  /dev/shm/*ipcator?*
	rm -rf lib/?*-build/
	rm -rf lib/archives/
	rm -rf docs/html/

# 查看一些 Makefile 中定义的变量.
.PHONY: print-vars
print-vars:
	@echo CXX = $(CXX)
	@echo DEBUG = $(DEBUG)
	@echo CXXFLAGS = $(CXXFLAGS)
	@echo LIBS = $(LIBS)
	@echo LIBARS = $(LIBARS)
	@echo LDFLAGS = $(LDFLAGS)
	@echo BUILD_INFO = $(BUILD_INFO)



# Local Variables:
# tab-width: 2
# End:
