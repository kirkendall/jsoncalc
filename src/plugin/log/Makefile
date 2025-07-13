LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/pluginlog.so
SRC=	log.c
OBJ=	log.o
CFLAGS=	-I$(INC)
CC=	gcc -fpic -g

$(ALL): pluginlog.so docs
	cp pluginlog.so $(PLUGIN)/pluginlog.so

pluginlog.so: $(OBJ)
	$(CC) -shared $(OBJ) -o pluginlog.so

docs:
	cp www/index.html $(WWW)/plugin/log.html
	[ -d $(WWW)/cmd/log ] || mkdir $(WWW)/cmd/log
	for i in www/cmd/*.html; do case "$$i" in *SKELETON.html)true;; *) cp "$$i" $(WWW)/cmd/log;; esac; done

clean:
	$(RM) $(PLUGIN)/pluginlog.so
	$(RM) pluginlog.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/log.html
	$(RM) $(WWW)/cmd/log/*.html
