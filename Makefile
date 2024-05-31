DATA=sampledata
HDRS=calc.h json.h
SRC=jsoncalc.c
OBJ=jsoncalc.o
TESTSRC=testcalc.c
TESTOBJ=testcalc.o
PROG=jsoncalc testcalc
LIBSRC=by.c calc.c calcfunc.c calcparse.c compare.c context.c copy.c debug.c \
	equal.c explain.c flat.c format.c grid.c groupby.c is.c length.c \
	mbstr.c memory.c parse.c print.c serialize.c sort.c text.c throw.c walk.c
LIBOBJ=by.o calc.o calcfunc.o calcparse.o compare.o context.o copy.o debug.o \
	equal.o explain.o flat.o format.o grid.o groupby.o is.o length.o \
	mbstr.o memory.o parse.o print.o serialize.o sort.o text.o throw.o walk.o
#CC=gcc -g -pg
#CC=gcc -g -O
CC=gcc -g
CFLAGS=-Wall -DJSON_DEBUG_MEMORY
LDFLAGS=-L.
LDLIBS=-lreadline

all: $(PROG)

jsoncalc: $(OBJ) libjson.a

testcalc: testcalc.o libjson.a

$(LIBOBJ): $(HDRS)

libjson.a: $(LIBOBJ)
	$(RM) libjson.a
	ar q libjson.a $(LIBOBJ)

test: $(DATA)/test.in testcalc
	@#./testcalc -e $(DATA)/test.in
	./testcalc -m $(DATA)/test.in

clean:
	$(RM) $(OBJ)
	$(RM) $(LIBOBJ)
	$(RM) $(TESTOBJ)
	$(RM) libjson.a
	$(RM) core

tags: $(SRC) $(LIBSRC) $(HDRS)
	elvtags $(SRC) $(LIBSRC) $(HDRS)

wc: $(SRC) $(LIBSRC) $(HDRS)
	wc $(SRC) $(LIBSRC) $(HDRS)|sort -n

gitinit:
	git add $(SRC) $(TESTSRC) $(LIBSRC) $(HDRS)
