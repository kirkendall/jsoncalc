LIB=	../../lib
INC=	../../include
BIN=	../../bin
HDR=	$(INC)/jx.h $(INC)/version.h
SRC=	jx.c interact.c batch.c complete.c
OBJ=	jx.o interact.o batch.o complete.o
CC=	gcc -g -fpic
CFLAGS=	-Wall -I$(INC) -DJX_DEBUG_MEMORY
LDFLAGS=-L$(LIB)
LDLIBS= -ljx -lreadline
LDSTATLIBS=-ljxplugins -lm -ltinfo


all: $(BIN)/jx $(BIN)/jxstatic

$(OBJ): $(HDR)

$(BIN)/jx $(BIN)/jxstatic: jx jxstatic
	cp jx jxstatic $(BIN)

jx: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o jx

jxstatic: $(OBJ)
	$(CC) -static $(LDFLAGS) $(OBJ) $(LDLIBS) $(LDSTATLIBS) -o jxstatic
	strip jcstatic

clean:
	$(RM) jx
	$(RM) jxstatic
	$(RM) $(OBJ)
