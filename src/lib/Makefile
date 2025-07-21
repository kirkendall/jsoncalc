LIB=	../../lib
INCLUDE=../../include
HDRS=	$(INCLUDE)/jsoncalc.h $(INCLUDE)/version.h
LIBS=	-ldl
LIBSRC=	by.c calc.c calcfunc.c calcparse.c compare.c config.c context.c \
	copy.c cmd.c datetime.c debug.c equal.c explain.c file.c find.c \
	flat.c format.c grid.c groupby.c is.c length.c mbstr.c memory.c \
	parse.c plugin.c print.c serialize.c sort.c text.c walk.c
LIBOBJ=	by.o calc.o calcfunc.o calcparse.o compare.o config.o context.o \
	copy.o cmd.o datetime.o debug.o equal.o explain.o file.o find.o \
	flat.o format.o grid.o groupby.o is.o length.o mbstr.o memory.o \
	parse.o print.o serialize.o sort.o text.o walk.o
#STATIC=	-DSTATICCACHE -DSTATICCSV -DSTATICCURL -DSTATICLOG -DSTATICMATH
STATIC=	-DSTATICCACHE -DSTATICCSV -DSTATICLOG -DSTATICMATH
#CC=gcc -g -pg
#CC=gcc -g -O
CC=gcc -g -fpic -Wall
CFLAGS=-Wall -DJSON_DEBUG_MEMORY -I$(INCLUDE)

all: $(LIB)/libjsoncalc.so $(LIB)/libjsoncalc.a

$(LIBOBJ): $(HDRS)

$(LIB)/libjsoncalc.so: $(LIBOBJ) plugin.o
	$(CC) $(LIBOBJ) plugin.o $(LIBS) -shared -o $(LIB)/libjsoncalc.so

$(LIB)/libjsoncalc.a: $(LIBOBJ) pluginstatic.o
	$(RM) $(LIB)/libjsoncalc.a
	ar q $(LIB)/libjsoncalc.a $(LIBOBJ) pluginstatic.o

pluginstatic.o: plugin.c
	$(CC) $(CFLAGS) $(STATIC) -c plugin.c -o pluginstatic.o

clean:
	$(RM) $(LIBOBJ)
	$(RM) plugin.o
	$(RM) pluginstatic.o
	$(RM) $(LIB)/libjsoncalc.so
	$(RM) $(LIB)/libjsoncalc.a

tags: $(LIBSRC)
	ctags $(LIBSRC)

