# Pass Note Makefile
INCLUDES=-D_GNU_SOURCE -I include -DENABLE_TOPT -DENABLE_NETSYNC -DENABLE_SAVENOTIFY -DENABLE_SESSION -DENABLE_ASBCLI
INDENT_FLAGS=-br -ce -i4 -bl -bli0 -bls -c4 -cdw -ci4 -cs -nbfda -l100 -lp -prs -nlp -nut -nbfde -npsl -nss
CC=gcc
LD=ld
CFLAGS=-O2 -Wall -Wextra -pedantic -Wstrict-prototypes -ffunction-sections -fdata-sections 
LDFLAGS=-s -Wl,--gc-sections -lmbedtls -lmbedcrypto -llz4

all: host_gtk3

prepare:
	@mkdir -p bin

appicon:
	@ld -r -b binary -o bin/app_icon.o res/app_icon.png

host_gtk3: prepare appicon
	@$(CC) $(CFLAGS) $(INCLUDES) \
	    `pkg-config --cflags gtk+-3.0` \
	    src/*.c addons/*.c bin/app_icon.o -o bin/passnote \
	    `pkg-config --libs gtk+-3.0` $(LDFLAGS)

host_gtk2: prepare appicon
	@$(CC) $(CFLAGS) $(LDFLAGS) $(INCLUDES) -DCONFIG_USE_GTK2 \
	    `pkg-config --cflags gtk+-2.0` \
	    src/*.c addons/*.c bin/app_icon.o -o bin/passnote \
	    `pkg-config --libs gtk+-2.0`

clean:
	@rm -rf bin

analyse:
	@cppcheck include/*.h src/*.c addon/*.c
	@scan-build make

indent:
	@indent $(INDENT_FLAGS) ./*/*.h
	@indent $(INDENT_FLAGS) ./*/*.c
	@rm -rf ./*/*~
