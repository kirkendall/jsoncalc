LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginxml.so
SRC=	xml.c
OBJ=	xml.o
LIBS=	
CFLAGS=	-I$(INC)
CC=	gcc -g -fpic

all: $(ALL)

$(ALL): pluginxml.so
	cp pluginxml.so $(PLUGIN)/pluginxml.so

pluginxml.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o pluginxml.so

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c xml.c

clean:
	$(RM) $(PLUGIN)/pluginxml.so
	$(RM) pluginxml.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/xml.html
