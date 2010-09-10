CC		= gcc

CFLAGS		= -Wall -g -I/opt/local/include -O
LDFLAGS		= -g
LIBS		= -L/opt/local/lib -lusb

COMMON_OBJS     = target.o
COMMON_OBJS	+= adapter-mpsse.o

PROG_OBJS	= milprog.o $(COMMON_OBJS)

all:		milprog

load:           demo1986ve91.srec
		milprog $<

milprog:	$(PROG_OBJS)
		$(CC) $(LDFLAGS) -o $@ $(PROG_OBJS) $(LIBS)

adapter-mpsse:	adapter-mpsse.c
		$(CC) $(LDFLAGS) $(CFLAGS) -DSTANDALONE -o $@ adapter-mpsse.c $(LIBS)

milprog.po:	*.c
		xgettext --from-code=utf-8 --keyword=_ milprog.c target.c adapter-lpt.c -o $@

milprog-ru.mo:	milprog-ru.po
		msgfmt -c -o $@ $<

milprog-ru-cp866.mo ru/LC_MESSAGES/milprog.mo: milprog-ru.po
		iconv -f utf-8 -t cp866 $< | sed 's/UTF-8/CP866/' | msgfmt -c -o $@ -
		cp milprog-ru-cp866.mo ru/LC_MESSAGES/milprog.mo

clean:
		rm -f *~ *.o core milprog adapter-mpsse milprog.po

install:	milprog #milprog-ru.mo
		install -c -s milprog /usr/local/bin/milprog
#		install -c -m 444 milprog-ru.mo /usr/local/share/locale/ru/LC_MESSAGES/milprog.mo
###
adapter-mpsse.o: adapter-mpsse.c adapter.h arm-jtag.h
milprog.o: milprog.c target.h localize.h
target.o: target.c target.h adapter.h arm-jtag.h localize.h
