LIB=../../lib
INC=../../include
CFLAGS=-DJSON_DEBUG_MEMORY -I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljson
CC=gcc -g

all: testcalc.out

testcalc.out: testcalc test.in
	./testcalc test.in

testcalc: testcalc.c $(LIB)/libjson.a
	$(CC) $(CFLAGS) $(LDFLAGS) testcalc.c $(LDLIBS) -o testcalc

clean:
	$(RM) testcalc
