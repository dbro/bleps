EXE=bleps
LIBUSB = libusb-1.0

CFLAGS = -W -Wall -O -g
CFLAGS += $(shell pkg-config $(LIBUSB) --cflags)
LDFLAGS = $(shell pkg-config $(LIBUSB) --libs)

BINDIR=/usr/local/bin

all:	$(EXE)

$(EXE): $(EXE).c
	$(CC) $(CFLAGS) $(LDFLAGS) $(EXE).c -o $(EXE)

install: $(EXE)
	install -m 755 $(EXE) $(BINDIR)

clean:
	rm -f $(EXE)

