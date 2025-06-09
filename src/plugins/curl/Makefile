LIB=	../../../plugin
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/plugincurl.so
SRC=	curl.c
OBJ=	curl.o
LIBS=	`curl-config --libs`
CFLAGS=	-I$(INC) `curl-config --cflags`
CC=	gcc -fpic -g

$(ALL): plugincurl.so
	cp plugincurl.so $(PLUGIN)/plugincurl.so

plugincurl.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincurl.so

clean:
	$(RM) plugincurl.so
	$(RM) $(OBJ)
