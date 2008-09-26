SHOPTS := -shared 
OBJS   := overcommit-memory.o overcommit.o ../lib/fd.o

all: overcommit-memory.so overcommit-util

overcommit-memory.so : $(OBJS)
	$(CC) $(SHOPTS) -o overcommit-memory.so $(OBJS)

overcommit-util : util.o overcommit.o ../lib/fd.o
	$(CC) -o overcommit-util util.o overcommit.o ../lib/fd.o -lpthread

.c.o :
	$(CC) -ggdb -I../lib -Wall $(CFLAGS) -o $@ -fPIC -c $< 


clean:
	rm -f *.o *.so overcommit-util
