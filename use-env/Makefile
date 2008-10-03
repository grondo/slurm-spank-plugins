
LIBDIR ?= /usr/lib$(shell uname -m | grep -q x86_64 && echo 64)

OBJS   := lex.yy.o use-env-parser.o ../lib/list.o log_msg.o ../lib/split.o
HDRS   := use-env.h ../lib/list.h ../lib/split.h log_msg.h use-env-parser.h
SHOPTS := -shared -Wl,--version-script=version.map


all: use-env.so test

install: use-env.so
	@mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	install -m0755 use-env.so $(DESTDIR)$(LIBDIR)/slurm

use-env.so : $(OBJS) use-env.o
	$(CC) $(SHOPTS) -o use-env.so $(OBJS) use-env.o 

test: $(OBJS) main.o
	$(CC) -ggdb -o test $(OBJS) main.o

check: test
	./test -f test.conf

.c.o :
	$(CC) -ggdb -I../lib -Wall $(CFLAGS) -o $@ -fPIC -c $< 

use-env-parser.c use-env-parser.h : use-env-parser.y
	bison -d -o use-env-parser.c $< 

lex.yy.c : use-env-parser.l use-env-parser.h 
	lex $<

clean: 
	rm -f test *.o use-env-parser.[ch] lex.yy.c *.so
