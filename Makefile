NAME=pxz
VERSION=4.999.9beta
CC=gcc
WARNINGS=-Wall -Wshadow -Wcast-align -Winline -Wextra -Wmissing-noreturn
CFLAGS+=-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DPXZ_BUILD_DATE=\"`date +%Y%m%d`\" -DPXZ_VERSION=\"$(VERSION)\"
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
	rm -rf $(NAME)-$(VERSION)
	mkdir $(NAME)-$(VERSION)
	cp $(SOURCES) $(NAME)-$(VERSION)
	tar fcJ $(NAME)-$(VERSION).`date +%Y%m%d`git.tar.xz $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION)

install: $(NAME)
	mkdir -p $(DESTDIR)$(BINDIR)
	cp $(NAME) $(DESTDIR)$(BINDIR)

ddd: $(NAME)
	ddd --args $(EXECUTE)

gdb: $(NAME)
	gdb --args $(EXECUTE)

time: $(NAME)
	time $(EXECUTE)

valgrind: $(NAME)
	valgrind -v --tool=memcheck --leak-check=yes $(EXECUTE)
