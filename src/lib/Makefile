LIB=	../../lib
INCLUDE=../../include
HDRS=	$(INCLUDE)/calc.h $(INCLUDE)/error.h $(INCLUDE)/json.h $(INCLUDE)/version.h
LIBSRC=	by.c calc.c calcfunc.c calcparse.c compare.c config.c context.c \
	copy.c cmd.c datetime.c debug.c equal.c explain.c file.c find.c \
	flat.c format.c grid.c groupby.c is.c length.c mbstr.c memory.c \
	parse.c print.c serialize.c sort.c text.c walk.c
LIBOBJ=	by.o calc.o calcfunc.o calcparse.o compare.o config.o context.o \
	copy.o cmd.o datetime.o debug.o equal.o explain.o file.o find.o \
	flat.o format.o grid.o groupby.o is.o length.o mbstr.o memory.o \
	parse.o print.o serialize.o sort.o text.o walk.o
#CC=gcc -g -pg
#CC=gcc -g -O
CC=gcc -g
CFLAGS=-Wall -DJSON_DEBUG_MEMORY -I$(INCLUDE)

all: $(LIB)/libjson.a

$(LIBOBJ): $(HDRS)

$(LIB)/libjson.a: $(LIBOBJ)
	$(RM) $(LIB)/libjson.a
	ar q $(LIB)/libjson.a $(LIBOBJ)

clean:
	$(RM) $(OBJ)
	$(RM) $(LIBOBJ)
	$(RM) $(LIB)/libjson.a
	$(RM) core

tags: $(LIBSRC)
	ctags $(LIBSRC)

