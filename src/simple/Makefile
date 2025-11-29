LIB=	../../lib
INC=	../../include
CFLAGS=	-I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljx

all: simple

simple: simple.c $(LIB)/libjx.so
	$(CC) $(CFLAGS) $(LDFLAGS) simple.c $(LDLIBS) -o simple

clean:
	$(RM) simple
