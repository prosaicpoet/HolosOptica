CC ?= gcc
PKGCONFIG = $(shell which pkg-config)

CFLAGS = $(shell $(PKGCONFIG) --cflags gtk+-3.0) -mwindows
LIBS = $(shell $(PKGCONFIG) --libs gtk+-3.0)

SRC = GTK3ImageViewer.c

OBJS = $(BUILT_SRC:.c=.o) $(SRC:.c=.o)

OBJS += app_icons.o

BIN = holosoptica

all: $(BIN)

gtk4: CFLAGS = $(shell $(PKGCONFIG) --cflags gtk4)
gtk4: LIBS = $(shell $(PKGCONFIG) --libs gtk4)
gtk4: $(BIN)

debug: CFLAGS += -g -DDEBUG
debug: $(BIN)

%.o: %.c
	$(CC) -c -o $(@F) $(CFLAGS) $<

$(BIN): $(OBJS)
	$(CC) -o $(@F) $(OBJS) $(LIBS) $(CFLAGS)

app_icons.o : app_icons.rc
	windres app_icons.rc -o app_icons.o

app_icons.rc : holosoptica.ico
	echo 1 ICON holosoptica.ico > app_icons.rc

clean:
	rm -f $(OBJS)
	rm -f $(BIN)
