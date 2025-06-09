LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/plugincsv.so
SRC=	csv.c
OBJ=	csv.o
LIBS=	-lm
CFLAGS=	-I$(INC)
CC=	gcc -g -fpic

$(ALL): plugincsv.so
	cp plugincsv.so $(PLUGIN)/plugincsv.so

plugincsv.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincsv.so
