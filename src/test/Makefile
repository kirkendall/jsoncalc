LIB=../../lib
INC=../../include
CFLAGS=-DJSON_DEBUG_MEMORY -I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljson
CC=gcc -g

all: testcalc.out testconfig

testcalc.out: testcalc test.in
	./testcalc test.in

testcalc: testcalc.c $(LIB)/libjson.a
	$(CC) $(CFLAGS) $(LDFLAGS) testcalc.c $(LDLIBS) -o testcalc

testconfig: testconfig.c $(LIB)/libjson.a
	$(CC) $(CFLAGS) $(LDFLAGS) testconfig.c $(LDLIBS) -o testconfig

clean:
	$(RM) testcalc
