LIB=	../../lib
INC=	../../include
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljson

all: simple

simple: simple.c $(LIB)/libjson.a
	$(CC) $(CFLAGS) $(LDFLAGS) simple.c $(LDLIBS) -o simple

clean:
	$(RM) simple
