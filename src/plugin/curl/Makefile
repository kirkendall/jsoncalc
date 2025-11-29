LIB=	../../../lib
INC=	../../../include
BIN=	../../../bin
PLUGIN=	../../../plugin
WWW=	../../../www

ALL=	$(PLUGIN)/plugincurl.so
SRC=	curl.c
OBJ=	curl.o
LIBS=	-L/usr/local/lib64 `curl-config --libs`
CFLAGS=	-I$(INC) `curl-config --cflags`
CC=	gcc -fpic -g

$(ALL): plugincurl.so
	cp plugincurl.so $(PLUGIN)/plugincurl.so

plugincurl.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LIBS) -o plugincurl.so

$(OBJ): $(SRC)

trycurl: trycurl.c curl.c
	$(CC) $(CFLAGS) trycurl.c $(LIBS) -L ../../../lib -ljx -o trycurl

clean:
	$(RM) $(PLUGIN)/plugincurl.so
	$(RM) plugincurl.so
	$(RM) $(OBJ)
	$(RM) $(WWW)/plugin/curl.html
	$(RM) $(WWW)/f/curl/*.html
