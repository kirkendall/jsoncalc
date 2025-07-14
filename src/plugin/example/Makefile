LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginexample.so
SRC=	example.c
OBJ=	example.o
LIBS=	
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): pluginexample.so
	cp pluginexample.so $(PLUGIN)/pluginexample.so

pluginexample.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o pluginexample.so

clean:
	$(RM) $(PLUGIN)/pluginexample.so
	$(RM) pluginexample.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/example.html
	$(RM) $(WWW)/f/example/*.html
	$(RM) $(WWW)/cmd/example/*.html
