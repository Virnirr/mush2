CCFLAGS = gcc -Wall -g -pedantic

LDFLAGS = gcc -Wall -g -pedantic

OBJMUSH = ~pn-cs357/Given/Mush/libmush/lib64/

LINKMUSH = ~pn-cs357/Given/Mush/libmush/include

mush2: mush2.o ~pn-cs357/Given/Mush/libmush/lib64/libmush.a
	$(LDFLAGS) -o mush2 -L $(OBJMUSH) mush2.o -lmush

mush2.o: mush2.c ~pn-cs357/Given/Mush/libmush/include/mush.h
	$(CCFLAGS) -c -I $(LINKMUSH) mush2.c

clean:
	rm *.o
	rm -f mush2
	echo DONE