LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincsv.so
SRC=	csv.c
OBJ=	csv.o
LIBS=	-lm
CFLAGS=	-I$(INC)
CC=	gcc -g -fpic

$(ALL): plugincsv.so docs
	cp plugincsv.so $(PLUGIN)/plugincsv.so

plugincsv.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincsv.so

docs:
	cp www/index.html $(WWW)/plugin/csv.html
	[ -d $(WWW)/f/csv ] || mkdir $(WWW)/f/csv
	for i in www/f/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/f/csv; esac; done
	for i in www/cmd/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/cmd/csv; esac; done

clean:
	$(RM) plugincsv.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/csv.html
	$(RM) $(WWW)/f/csv/*.html
	$(RM) $(WWW)/cmd/csv/*.html
