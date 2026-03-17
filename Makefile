NAME=pxz
VERSION=4.999.9beta
CFLAGS?=-O2 -Wall -Wshadow -Wcast-align -Winline -Wextra -Wmissing-noreturn
CFLAGS+=-fopenmp -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CFLAGS+=$(shell pkg-config --cflags liblzma 2>/dev/null)
LDFLAGS+=$(shell pkg-config --libs liblzma 2>/dev/null || echo -llzma)
SOURCES=$(NAME).c
DIST_FILES=$(SOURCES) Makefile COPYING $(NAME).1
OBJECTS=
EXECUTE=./$(NAME) CONTENTS.cpio
PREFIX?=/usr
BINDIR?=$(PREFIX)/bin
MANDIR?=$(PREFIX)/share/man

.PHONY: all
all: $(OBJECTS) $(NAME)

$(NAME): $(SOURCES) $(OBJECTS)
	$(CC) -o $(NAME) $(CPPFLAGS) $(CFLAGS) $(NAME).c $(OBJECTS) $(LDFLAGS) -DPXZ_BUILD_DATE=\"`date +%Y%m%d`\" -DPXZ_VERSION=\"$(VERSION)\"

.PHONY: clean
clean:
	rm -f *.o $(NAME) COPYING.test test.xz

.PHONY: distclean
distclean: clean
	rm -f *~

.PHONY: dist
dist: $(NAME)
	LANG=C
	rm -rf $(NAME)-$(VERSION)
	mkdir $(NAME)-$(VERSION)
	cp $(DIST_FILES) $(NAME)-$(VERSION)
	tar fcJ $(NAME)-$(VERSION).`date +%Y%m%d`git.tar.xz $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION)

.PHONY: install
install: $(NAME)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(NAME) $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 $(NAME).1 $(DESTDIR)$(MANDIR)/man1

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(MANDIR)/man1/$(NAME).1

.PHONY: ddd
ddd: $(NAME)
	ddd --args $(EXECUTE)

.PHONY: gdb
gdb: $(NAME)
	gdb --args $(EXECUTE)

.PHONY: time
time: $(NAME)
	time $(EXECUTE)

.PHONY: valgrind
valgrind: $(NAME)
	valgrind -v --tool=memcheck --leak-check=yes $(EXECUTE)


test.xz: $(NAME) COPYING
	./$(NAME) -3 -c COPYING > test.xz

.PHONY: check
check: test.xz
	xz -dc test.xz > COPYING.test
	cmp COPYING COPYING.test
	./$(NAME) -dc test.xz > /dev/null
	@echo "All tests passed."
