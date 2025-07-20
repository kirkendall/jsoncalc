LIB=	../../lib
INC=	../../include
BIN=	../../bin
HDR=	$(INC)/jsoncalc.h $(INC)/version.h
SRC=	jsoncalc.c interact.c batch.c complete.c
OBJ=	jsoncalc.o interact.o batch.o complete.o
CC=	gcc -g -fpic
CFLAGS=	-Wall -I$(INC) -DJSON_DEBUG_MEMORY
LDFLAGS=-L$(LIB)
LDLIBS= -ljsoncalc -lreadline


all: $(BIN)/jsoncalc

$(OBJ): $(HDR)

$(BIN)/jsoncalc: jsoncalc
	cp jsoncalc $(BIN)

jsoncalc: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o jsoncalc

clean:
	$(RM) jsoncalc
	$(RM) $(OBJ)
