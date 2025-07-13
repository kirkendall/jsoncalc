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

$(ALL): pluginexample.so docs
	cp pluginexample.so $(PLUGIN)/pluginexample.so

pluginexample.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o pluginexample.so

docs:
	cp www/index.html $(WWW)/plugin/example.html
	[ -d $(WWW)/f/example ] || mkdir $(WWW)/f/example
	for i in www/f/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/f/example; esac; done
	[ -d $(WWW)/cmd/example ] || mkdir $(WWW)/cmd/example
	for i in www/cmd/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/cmd/example; esac; done

clean:
	$(RM) $(PLUGIN)/pluginexample.so
	$(RM) pluginexample.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/example.html
	$(RM) $(WWW)/f/example/*.html
	$(RM) $(WWW)/cmd/example/*.html
