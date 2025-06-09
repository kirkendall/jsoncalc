LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/pluginlog.so
SRC=	log.c
OBJ=	log.o
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): pluginlog.so
	cp pluginlog.so $(PLUGIN)/pluginlog.so

pluginlog.so: $(OBJ)
	$(CC) -shared $(OBJ) -o pluginlog.so
