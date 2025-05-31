LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/libcsv.so
SRC=	csv.c
OBJ=	csv.o
LIBS=	-lm
CFLAGS=	-I$(INC)
CC=	gcc -g -fpic

$(ALL): libcsv.so
	cp libcsv.so $(PLUGIN)/libcsv.so

libcsv.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o libcsv.so
