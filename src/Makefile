all:
	make -C lib -e
	for i in */.; do [ $$i = lib/. ] || make -C $$i -e; done

clean:
	for i in */.; do make -C $$i -e clean; done
