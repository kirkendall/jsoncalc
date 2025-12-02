LIB=	../../lib
INCLUDE=../../include
HDRS=	$(INCLUDE)/jx.h $(INCLUDE)/version.h
LIBS=	-ldl
LIBSRC=	by.c blob.c calc.c calcfunc.c calcparse.c compare.c config.c context.c \
	copy.c cmd.c datetime.c debug.c defer.c diff.c equal.c explain.c \
	file.c find.c flat.c format.c grid.c is.c length.c mbstr.c memory.c \
	parse.c plugin.c print.c serialize.c sort.c text.c user.c walk.c
LIBOBJ=	by.o blob.o calc.o calcfunc.o calcparse.o compare.o config.o context.o \
	copy.o cmd.o datetime.o debug.o defer.o diff.o equal.o explain.o \
	file.o find.o flat.o format.o grid.o is.o length.o mbstr.o memory.o \
	parse.o print.o serialize.o sort.o text.o user.o walk.o
#STATIC=	-DSTATICCACHE -DSTATICCSV -DSTATICCURL -DSTATICLOG -DSTATICMATH -DSTATICXML
STATIC=	-DSTATICCACHE -DSTATICCSV -DSTATICLOG -DSTATICMATH
#CC=gcc -g -pg
#CC=gcc -g -O
CC=gcc -g -fpic -Wall
CFLAGS=-Wall -DJX_DEBUG_MEMORY -I$(INCLUDE)

all: $(LIB)/libjx.so $(LIB)/libjx.a

$(LIBOBJ): $(HDRS)

$(LIB)/libjx.so: $(LIBOBJ) plugin.o
	$(CC) $(LIBOBJ) plugin.o $(LIBS) -shared -o $(LIB)/libjx.so

$(LIB)/libjx.a: $(LIBOBJ) pluginstatic.o
	$(RM) $(LIB)/libjx.a
	ar q $(LIB)/libjx.a $(LIBOBJ) pluginstatic.o

pluginstatic.o: plugin.c
	$(CC) $(CFLAGS) $(STATIC) -c plugin.c -o pluginstatic.o

clean:
	$(RM) $(LIBOBJ)
	$(RM) plugin.o
	$(RM) pluginstatic.o
	$(RM) $(LIB)/libjx.so
	$(RM) $(LIB)/libjx.a

tags: $(LIBSRC)
	ctags $(LIBSRC)

