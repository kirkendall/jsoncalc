LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/liblog.so
SRC=	log.c
OBJ=	log.o
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): liblog.so
	cp liblog.so $(PLUGIN)/liblog.so

liblog.so: $(OBJ)
	$(CC) -shared $(OBJ) -o liblog.so
