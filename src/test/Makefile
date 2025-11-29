LIB=../../lib
INC=../../include
CFLAGS=-DJX_DEBUG_MEMORY -I$(INC)
LDFLAGS=-L$(LIB)
LDLIBS=	-ljx
CC=gcc -g

all: testcalc.out testconfig

testcalc.out: testcalc test.in
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH):$(LIB) ./testcalc test.in

testcalc: testcalc.c
	$(CC) $(CFLAGS) $(LDFLAGS) testcalc.c $(LDLIBS) -o testcalc

testconfig: testconfig.c
	$(CC) $(CFLAGS) $(LDFLAGS) testconfig.c $(LDLIBS) -o testconfig

clean:
	$(RM) testcalc
