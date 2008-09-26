NAME   := cpuset

FLAGS  := -ggdb -Wall -I../lib
SHOPTS := -shared -Wl,--version-script=version.map
LLIBS  := -lbitmask -lcpuset -ldl -lfl
OBJS   := nodemap.o util.o create.o log.o slurm.o \
	  conf.o conf-lexer.o conf-parser.o \
          ../lib/fd.o ../lib/list.o ../lib/split.o

all: $(NAME).so test cpuset_release_agent pam_slurm_cpuset.so

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
