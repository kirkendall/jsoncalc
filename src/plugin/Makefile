LIB=	../../lib

all:
	for i in */.; do make -C $$i -e; done
	$(RM) $(LIB)/jsoncalcplugins.a
	ar q $(LIB)/libjsoncalcplugins.a */*.o 

clean:
	for i in */.; do make -C $$i -e clean; done
	$(RM) $(LIB)/jsoncalcplugins.a
