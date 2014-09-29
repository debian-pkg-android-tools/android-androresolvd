DEBUG=0

ifeq ($(DEBUG),0)
CFLAGS=-s
else
CFLAGS=-ggdb
endif

CFLAGS+=-I. -Wall
SBINDIR=$(DESTDIR)/usr/sbin

androresolvd: system_properties.o androresolvd.c

install: androresolvd
	mkdir -p $(SBINDIR)
	install -m 755 androresolvd $(SBINDIR)

clean:
	rm -vf *.o
	rm -vf androresolvd
	rm -vf nohup.out
	rm -vf *~
	rm -vf debian/*~

