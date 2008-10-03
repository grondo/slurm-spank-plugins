LIBDIR     ?= /usr/lib$(shell uname -m | grep -q x86_64 && echo 64)
LIBEXECDIR ?= /usr/libexec
PACKAGE    ?= slurm-spank-plugins

SHOPTS := -shared 
OBJS   := overcommit-memory.o overcommit.o ../lib/fd.o

all: overcommit-memory.so overcommit-util

install: 
	mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	install -m0755 overcommit-memory.so $(DESTDIR)$(LIBDIR)/slurm/
	mkdir -p --mode=0755 $(DESTDIR)$(LIBEXECDIR)/$(PACKAGE)
	install -m0755 overcommit-util $(DESTDIR)$(LIBEXECDIR)/$(PACKAGE)/

overcommit-memory.so : $(OBJS)
	$(CC) $(SHOPTS) -o overcommit-memory.so $(OBJS)

overcommit-util : util.o overcommit.o ../lib/fd.o
	$(CC) -o overcommit-util util.o overcommit.o ../lib/fd.o -lpthread

.c.o :
	$(CC) -ggdb -I../lib -Wall $(CFLAGS) -o $@ -fPIC -c $< 


clean:
	rm -f *.o *.so overcommit-util
