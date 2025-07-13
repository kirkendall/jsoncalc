LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincurl.so
SRC=	curl.c
OBJ=	curl.o
LIBS=	`curl-config --libs`
CFLAGS=	-I$(INC) `curl-config --cflags`
CC=	gcc -fpic -g

$(ALL): plugincurl.so docs
	cp plugincurl.so $(PLUGIN)/plugincurl.so

plugincurl.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincurl.so

docs:
	cp www/index.html $(WWW)/plugin/curl.html
	[ -d $(WWW)/f/curl ] || mkdir $(WWW)/f/curl
	for i in www/f/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/f/curl; esac; done

clean:
	$(RM) $(PLUGIN)/plugincurl.so
	$(RM) plugincurl.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/curl.html
	$(RM) $(WWW)/f/curl/*.html
