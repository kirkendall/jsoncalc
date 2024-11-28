LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

OBJ=	log.o
CFLAGS=	-fpic -I$(INC)

liblog.so: $(OBJ)
	$(CC) -shared -o liblog.so $(OBJ)
