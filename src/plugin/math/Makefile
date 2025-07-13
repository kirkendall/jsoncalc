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
	
docs:
	cp www/index.html $(WWW)/plugin/math.html
	[ -d $(WWW)/f/math ] || mkdir $(WWW)/f/math
	for i in www/f/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/f/cache; esac; done

clean:
	$(RM) $(PLUGIN)/pluginmath.so
	$(RM) pluginmath.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/math.html
	(RM) $(WWW)/f/math/*.html
