LIB=	../../lib
INC=	../../include
BIN=	../../bin
HDR=	$(INC)/calc.h $(INC)/error.h $(INC)/json.h $(INC)/version.h
SRC=	jsoncalc.c interact.c batch.c complete.c
OBJ=	jsoncalc.o interact.o batch.o complete.o
CC=	gcc -g -fpic
CFLAGS=	-Wall -I$(INC) -DJSON_DEBUG_MEMORY
LDFLAGS=-L$(LIB)
LDLIBS= -ljson -lreadline


all: $(BIN)/jsoncalc

$(OBJ): $(HDR)

$(BIN)/jsoncalc: jsoncalc
	cp jsoncalc $(BIN)

jsoncalc: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o jsoncalc

clean:
	$(RM) jsoncalc
	$(RM) $(OBJ)
