LUA_VER    ?= 5.1
LIBDIR     ?= /usr/local/lib
LUA_OBJDIR ?= $(LIBDIR)/lua/$(LUA_VER)
PREFIX     ?= /usr/local

CFLAGS     += -Wall -ggdb

.SUFFIXES: .c .o .so

.c.o:
	$(CC) $(CFLAGS) -o $@ -fPIC -c $<

schedutils.so: lua-schedutils.o cpuset-str.o
	$(CC) -shared -o $*.so $^ -llua $(LDFLAGS)

check: schedutils.so
	@(cd tests && LUA_CPATH=../?.so ./lunit tests.lua)

check-coverage:
	make clean
	make schedutils.so CFLAGS="-fprofile-arcs -ftest-coverage" LDFLAGS=-lgcov
	make check
	gcov lua-schedutils.c
	gcov cpuset-str.c

clean:
	rm -f *.so *.o *.gcov *.gcda *.gcno *.core

install:
	install -D -m0644 schedutils.so $(DESTDIR)$(LUA_OBJDIR)/schedutils.so
