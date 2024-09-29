BIN=	`pwd`/bin
INC=	`pwd`/include
LIB=	`pwd`/lib
PLUGIN=	`pwd`/plugin
CFLAGS=	-I$(INC) -DJSON_DEBUG_MEMORY
LDFLAGS=-L$(LIB)

all:
	BIN="$(BIN)" INC="$(INC)" LIB="$(LIB)" PLUGIN="$(PLUGIN)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" make -C src -e

clean:
	BIN="$(BIN)" LIB="$(LIB)" PLUGIN="$(PLUGIN)" make -C src -e clean

tags:
	ctags `find . -name '*.[ch]' -print`
