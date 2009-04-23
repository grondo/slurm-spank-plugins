PACKAGE    ?= slurm-spank-plugins

LIBNAME    ?= lib$(shell uname -m | grep -q x86_64 && echo 64)
LIBDIR     ?= /usr/$(LIBNAME)
BINDIR     ?= /usr/bin
SBINDIR    ?= /sbin
LIBEXECDIR ?= /usr/libexec

export LIBNAME LIBDIR BINDIR SBINDIR LIBEXECDIR PACKAGE

CFLAGS   = -Wall -ggdb

PLUGINS = \
   renice.so \
   system-safe.so \
   iotrace.so \
   tmpdir.so \
   auto-affinity.so \
   pty.so \
   addr-no-randomize.so \
   preserve-env.so \
   private-mount.so

LIBRARIES = \
   system-safe-preload.so \

ifeq ($(BUILD_LLNL_ONLY), 1)
   PLUGINS += oom-detect.so
endif

SUBDIRS = \
    use-env \
    overcommit-memory

ifeq ($(BUILD_CPUSET), 1)
  SUBDIRS += cpuset	
endif

all: $(PLUGINS) $(LIBRARIES) subdirs

.SUFFIXES: .c .o .so

.c.o: 
	$(CC) $(CFLAGS) -o $@ -fPIC -c $<
.o.so: 
	$(CC) -shared -o $*.so $< $(LIBS)

subdirs: 
	@for d in $(SUBDIRS); do make -C $$d; done

system-safe-preload.so : system-safe-preload.o
	$(CC) -shared -o $*.so $< -ldl 

auto-affinity.so : auto-affinity.o lib/split.o lib/list.o lib/fd.o
	$(CC) -shared -o $*.so auto-affinity.o lib/split.o lib/list.o -lslurm

preserve-env.so : preserve-env.o lib/list.o
	$(CC) -shared -o $*.so preserve-env.o lib/list.o

private-mount.so : private-mount.o lib/list.o lib/split.o
	$(CC) -shared -o $*.so private-mount.o lib/list.o lib/split.o

pty.so : pty.o
	$(CC) -shared -o $*.so $< -lutil

clean: subdirs-clean
	rm -f *.so *.o lib/*.o

install:
	@mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	@for p in $(PLUGINS); do \
	   echo "Installing $$p in $(LIBDIR)/slurm"; \
	   install -m0755 $$p $(DESTDIR)$(LIBDIR)/slurm; \
	 done
	@for f in $(LIBRARIES); do \
	   echo "Installing $$f in $(LIBDIR)"; \
	   install -m0755 $$f $(DESTDIR)$(LIBDIR); \
	 done
	@for d in $(SUBDIRS); do \
	   make -C $$d DESTDIR=$(DESTDIR) install; \
	 done

subdirs-clean:
	@for d in $(SUBDIRS); do make -C $$d clean; done

