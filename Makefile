NAME=pxz
VERSION=4.999.9beta
CFLAGS?=-O2 -Wall -Wshadow -Wcast-align -Winline -Wextra -Wmissing-noreturn -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CFLAGS+=-fopenmp
LDFLAGS+=-llzma
SOURCES=$(NAME).c Makefile COPYING $(NAME).1
OBJECTS=
EXECUTE=./$(NAME) CONTENTS.cpio
BINDIR?=/usr/bin
MANDIR?=/usr/share/man

all: $(OBJECTS) $(NAME)

$(NAME): $(SOURCES) $(OBJECTS)
	$(CC) -o $(NAME) $(CPPFLAGS) $(CFLAGS) $(NAME).c $(OBJECTS) $(LDFLAGS) -DPXZ_BUILD_DATE=\"`date +%Y%m%d`\" -DPXZ_VERSION=\"$(VERSION)\"

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
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp $(NAME).1 $(DESTDIR)$(MANDIR)/man1

ddd: $(NAME)
	ddd --args $(EXECUTE)

gdb: $(NAME)
	gdb --args $(EXECUTE)

time: $(NAME)
	time $(EXECUTE)

valgrind: $(NAME)
	valgrind -v --tool=memcheck --leak-check=yes $(EXECUTE)
