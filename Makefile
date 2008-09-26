
CFLAGS = -Wall -ggdb

all: renice.so \
     oom-detect.so \
     system-safe-preload.so system-safe.so \
     iotrace.so \
     tmpdir.so \
     auto-affinity.so \
     pty.so \
     addr-no-randomize.so \
     preserve-env.so \
     subdirs

SUBDIRS = use-env overcommit-memory cpuset

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

pty.so : pty.o
	$(CC) -shared -o $*.so $< -lutil

clean: subdirs-clean
	rm -f *.so *.o lib/*.o

subdirs-clean:
	@for d in $(SUBDIRS); do make -C $$d clean; done

