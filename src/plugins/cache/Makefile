LIB=	../../../plugin
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincache.so
SRC=	cache.c
OBJ=	cache.o
LIBS=	`cache-config --libs`
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): plugincache.so docs
	cp plugincache.so $(PLUGIN)/plugincache.so

plugincache.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincache.so

docs:
	cp www/index.html $(WWW)/plugin/cache.html
	[ -d $(WWW)/f/cache ] || mkdir $(WWW)/f/cache
	for i in www/f/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/f/cache; esac; done

clean:
	$(RM) plugincache.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/cache.html
	$(RM) $(WWW)/f/cache/*.html
