
VER	= 0.1

CFLAGS	= -fPIC -Wall -O2 -I. -D_REENTRANT -D_GNU_SOURCE

ALL:	liblockfile.so.$(VER) nfslock.so.$(VER)

liblockfile.a:	lockfile.o
		ar rv liblockfile.a lockfile.o

liblockfile.so.$(VER): liblockfile.a
		$(CC) -fPIC -shared -Wl,-soname,liblockfile.so.0 \
			-o liblockfile.so.$(VER) lockfile.o -lc

nfslock.so.$(VER):	nfslock.o
		$(CC) -fPIC -shared -Wl,-soname,nfslock.so.0 \
			-o nfslock.so.$(VER) nfslock.o

clean:
	rm -f *.a *.o *.so *.so.*
