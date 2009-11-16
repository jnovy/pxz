CC=gcc
NAME=pxz
WARNINGS=-Wall -Wshadow -Wcast-align -Wunreachable-code -Winline -Wextra -Wmissing-noreturn
CFLAGS+=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
#CFLAGS+=-DDEBUG -ggdb3
CFLAGS+=-O2 -fopenmp
LDFLAGS+=-llzma
SOURCES=$(NAME).c Makefile
OBJECTS=
EXECUTE=./$(NAME) CONTENTS.cpio
BINDIR?=/usr/bin
MANDIR?=/usr/share/man

all: $(OBJECTS) $(NAME)

$(NAME): $(SOURCES) $(OBJECTS)
	$(CC) $(WARNINGS) $(OBJECTS) $(CFLAGS) $(LDFLAGS) $(NAME).c -o $(NAME)

clean:
	rm -f *.o $(NAME)

distclean: clean
	rm -f *~

dist: $(NAME)
	LANG=C
	rm -rf $(NAME)
	mkdir $(NAME)
	cp $(SOURCES) $(NAME)
	tar fcj $(NAME)-`date +%d.%m.%Y`.tar.bz2 $(NAME)
	rm -rf $(NAME)

install: $(NAME)
	cp $(NAME) $(BINDIR)

ddd: $(NAME)
	ddd --args $(EXECUTE)

gdb: $(NAME)
	gdb --args $(EXECUTE)

time: $(NAME)
	time $(EXECUTE)

valgrind: $(NAME)
	valgrind -v --tool=memcheck --leak-check=yes $(EXECUTE)
