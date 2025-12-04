LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin

ALL=	$(PLUGIN)/pluginmath.so
SRC=	math.c
OBJ=	math.o
LIBS=	-lm
CFLAGS=	-I$(INC)
CC=	gcc -fpic

$(ALL): pluginmath.so
	cp pluginmath.so $(PLUGIN)/pluginmath.so

pluginmath.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o pluginmath.so
	
clean:
	$(RM) $(PLUGIN)/pluginmath.so
	$(RM) pluginmath.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/math.html
	$(RM) $(WWW)/f/math/*.html
