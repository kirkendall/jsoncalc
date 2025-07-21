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
LDSTATLIBS=-ljsoncalcplugins -lm -ltinfo


all: $(BIN)/jsoncalc $(BIN)/jcstatic

$(OBJ): $(HDR)

$(BIN)/jsoncalc $(BIN)/jcstatic: jsoncalc jcstatic
	cp jsoncalc jcstatic $(BIN)

jsoncalc: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o jsoncalc

jcstatic: $(OBJ)
	$(CC) -static $(LDFLAGS) $(OBJ) $(LDLIBS) $(LDSTATLIBS) -o jcstatic
	#strip jcstatic

clean:
	$(RM) jsoncalc
	$(RM) $(OBJ)
