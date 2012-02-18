PLUGINS = \
   lua.so \

SUBDIRS = \
   lua-schedutils

ifeq ($(HAVE_SPANK_OPTION_GETOPT), 1)
  EXTRA_CFLAGS = -DHAVE_SPANK_OPTION_GETOPT
endif

all: $(PLUGINS) subdirs

check: subdirs-check

subdirs:
	@for d in $(SUBDIRS); do make -C $$d; done

subdirs-check:
	@for d in $(SUBDIRS); do make -C $$d check; done


.SUFFIXES: .c .o .so

.c.o:
	$(CC) -I.. $(EXTRA_CFLAGS) $(CFLAGS) -o $@ -fPIC -c $<

lua.so : lua.o ../lib/list.o
	$(CC) -shared -o $*.so $^ -llua

clean: subdirs-clean
	rm -f *.so *.o lib/*.o

subdirs-clean:
	@for d in $(SUBDIRS); do make -C $$d clean; done

install: subdirs-install
	@mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	@for p in $(PLUGINS); do \
	   echo "Installing $$p in $(LIBDIR)/slurm"; \
	   install -m0755 $$p $(DESTDIR)$(LIBDIR)/slurm; \
	 done

subdirs-install:
	@for d in $(SUBDIRS); do \
		make -C $$d DESTDIR=$(DESTDIR) install; \
	done

