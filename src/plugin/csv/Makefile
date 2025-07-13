LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincsv.so
SRC=	csv.c
OBJ=	csv.o
LIBS=	
CFLAGS=	-I$(INC)
CC=	gcc -g -fpic

$(ALL): plugincsv.so docs
	cp plugincsv.so $(PLUGIN)/plugincsv.so

plugincsv.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincsv.so

docs:
	cp www/index.html $(WWW)/plugin/csv.html

clean:
	$(RM) $(PLUGIN)/plugincsv.so
	$(RM) plugincsv.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/csv.html
