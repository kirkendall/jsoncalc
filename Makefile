BIN=	`pwd`/bin
INC=	`pwd`/include
LIB=	`pwd`/lib
PLUGIN=	`pwd`/plugin
CFLAGS=	-I$(INC) -DJSON_DEBUG_MEMORY
#CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
CC=	gcc -fpic

all:
	BIN="$(BIN)" INC="$(INC)" LIB="$(LIB)" PLUGIN="$(PLUGIN)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" make -C src -e

clean:
	BIN="$(BIN)" LIB="$(LIB)" PLUGIN="$(PLUGIN)" make -C src -e clean

.PHONY: tags

tags:
	ctags `find . -name '*.[ch]' -print`

wc:
	@echo `find . -name '*.[ch]' -exec cat {} \; | wc -l` lines of C code
	@echo `find . -name '*.jc' -exec cat {} \; | wc -l` lines of JsonCalc scripts
	@echo `find www man -regex '.*\.\(html\|css\|sh\|man\|1\)' -exec cat {} \; | wc -l` lines of documentation
