CC ?= gcc
PKGCONFIG = $(shell which pkg-config)

CFLAGS = $(shell $(PKGCONFIG) --cflags gtk+-3.0)
LIBS = $(shell $(PKGCONFIG) --libs gtk+-3.0)

SRC = GTK3ImageViewer.c

OBJS = $(BUILT_SRC:.c=.o) $(SRC:.c=.o)

BIN = imgview2monitors

all: $(BIN)

gtk4: CFLAGS = $(shell $(PKGCONFIG) --cflags gtk4)
gtk4: LIBS = $(shell $(PKGCONFIG) --libs gtk4)
gtk4: $(BIN)

debug: CFLAGS += -g
debug: $(BIN)

%.o: %.c
	$(CC) -c -o $(@F) $(CFLAGS) $<

$(BIN): $(OBJS)
	$(CC) -o $(@F) $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS)
	rm -f $(BIN)
