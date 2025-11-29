LIB=	../../lib

all:
	for i in */.; do make -C $$i -e; done
	$(RM) $(LIB)/jxplugins.a
	ar q $(LIB)/libjxplugins.a */*.o 

clean:
	for i in */.; do make -C $$i -e clean; done
	$(RM) $(LIB)/jxplugins.a
