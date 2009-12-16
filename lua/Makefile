PLUGINS = \
   lua.so \

all: $(PLUGINS)

.SUFFIXES: .c .o .so

.c.o:
	$(CC) -I.. $(CFLAGS) -o $@ -fPIC -c $<

lua.so : lua.o ../lib/list.o
	$(CC) -shared -o $*.so $^ -llua

clean:
	rm -f *.so *.o lib/*.o

install:
	@mkdir -p --mode=0755 $(DESTDIR)$(LIBDIR)/slurm
	@for p in $(PLUGINS); do \
	   echo "Installing $$p in $(LIBDIR)/slurm"; \
	   install -m0755 $$p $(DESTDIR)$(LIBDIR)/slurm; \
	 done
