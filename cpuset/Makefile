NAME    := cpuset
SBINDIR ?= /sbin
LIBNAME ?= lib$(shell uname -m | grep -q x86_64 && echo 64)
LIBDIR  ?= /usr/$(LIBNAME)
MANDIR  ?= /usr/share/man
PAMDIR  := /$(LIBNAME)/security

FLAGS   := -ggdb -Wall -I../lib
SHOPTS  := -shared -Wl,--version-script=version.map
LLIBS   := -lslurm -lbitmask -lcpuset -ldl -lfl
OBJS    := nodemap.o util.o create.o log.o slurm.o \
           conf.o conf-lexer.o conf-parser.o \
           ../lib/fd.o ../lib/list.o ../lib/split.o

MAN8    := slurm-cpuset.8 pam_slurm_cpuset.8
MAN1    := use-cpusets.1

all: $(NAME).so test cpuset_release_agent pam_slurm_cpuset.so

install:
	mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	install -m0755 cpuset.so $(DESTDIR)$(LIBDIR)/slurm
	mkdir -p --mode=0755 $(DESTDIR)$(PAMDIR)
	install -m0755 pam_slurm_cpuset.so $(DESTDIR)$(PAMDIR)
	mkdir -p --mode=0755 $(DESTDIR)$(SBINDIR)
	install -m0755 cpuset_release_agent $(DESTDIR)$(SBINDIR)/
	mkdir -p --mode=0755 $(DESTDIR)$(MANDIR)/man1
	mkdir -p --mode=0755 $(DESTDIR)$(MANDIR)/man8
	install -m0644 $(MAN8) $(DESTDIR)$(MANDIR)/man8
	install -m0644 $(MAN1) $(DESTDIR)$(MANDIR)/man1

$(NAME).so: $(OBJS) $(NAME).o
	$(CC) $(SHOPTS) -o $(NAME).so $(OBJS) $(NAME).o $(LLIBS)

test: test.o $(OBJS)
	$(CC) -o test $(OBJS) test.o $(LLIBS) 

cpuset_release_agent: release-agent.o $(OBJS)
	$(CC) -o cpuset_release_agent $(OBJS) release-agent.o $(LLIBS)


pam_slurm_cpuset.so : $(OBJS) pam_slurm_cpuset.o ../lib/hostlist.o
	$(CC) -shared -o pam_slurm_cpuset.so $(OBJS) ../lib/hostlist.o \
		pam_slurm_cpuset.o -lbitmask $(LLIBS) -lpam -lpam_misc
.c.o: 
	$(CC) $(CFLAGS) $(FLAGS) -o $@ -fPIC -c $<

conf.o : conf-parser.h

conf-lexer.c : conf-parser.l conf-parser.h
	flex -oconf-lexer.c conf-parser.l

conf-parser.c conf-parser.h : conf-parser.y
	bison -d -oconf-parser.c conf-parser.y

clean:
	-rm -f *.o *.so conf-parser.[ch] conf-lexer.c cpuset_release_agent test
