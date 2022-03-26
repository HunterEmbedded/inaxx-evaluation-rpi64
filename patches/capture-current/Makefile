CFLAGS = -Wall -g -pthread -isysroot $(ROOTFSDIR) -I usr/include
LDFLAGS = -Wl,--sysroot=$(ROOTFSDIR) -L$(ROOTFSDIR)/usr/lib/arm-linux-gnueabihf/ 
LDLIBS = -lrt -lsqlite3
PROGS = capture-current-iio

all: $(PROGS)

capture-current-iio: capture-current-iio.c iio_utils.c sql-manager.c
	$(CC)  $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o
	rm -f $(PROGS)

install:
	cp $(PROGS) $(TARGETDIR)/
