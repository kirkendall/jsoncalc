LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/libmath.so
SRC=	math.c
OBJ=	math.o
CFLAGS=	-fpic -I$(INC)
CC=	gcc -fpic

$(ALL): libmath.so
	cp libmath.so $(PLUGIN)/libmath.so

libmath.so: $(OBJ)
	$(CC) -shared $(OBJ) -lm -o libmath.so
