LIB=	../../lib
INC=	../../include
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljsoncalc

all: simple

simple: simple.c $(LIB)/libjsoncalc.so
	$(CC) $(CFLAGS) $(LDFLAGS) simple.c $(LDLIBS) -o simple

clean:
	$(RM) simple
