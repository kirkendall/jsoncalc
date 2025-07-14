LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincache.so
SRC=	cache.c
OBJ=	cache.o
LIBS=	
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): plugincache.so
	cp plugincache.so $(PLUGIN)/plugincache.so

plugincache.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincache.so

clean:
	$(RM) $(PLUGIN)/plugincache.so
	$(RM) plugincache.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/cache.html
	$(RM) $(WWW)/f/cache/*.html
