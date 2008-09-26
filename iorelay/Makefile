CFLAGS = -Wall -ggdb

all: iorelay.so

.SUFFIXES: .c .o .so

.c.o: 
	$(CC) $(CFLAGS) -o $@ -fPIC -c $<
.o.so: 
	$(CC) -shared -o $*.so $< $(LIBS)

clean:
	rm -f *.so *.o
