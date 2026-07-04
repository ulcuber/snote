CC=gcc
CFLAGS=-std=c99 -O3 -Wall $(shell pkg-config --cflags xft)
LDFLAGS=-lX11 -lXext -lXft -lXrender -lfontconfig -lfreetype
SOURCES=$(wildcard src/*.c)
OBJS=$(patsubst src/%.c,obj/%.o,$(SOURCES))
TARGET=bin/snote

ifdef NO_VERBOSE
    CFLAGS += -DNO_VERBOSE
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -rf $(OBJS)
clean-all:
	rm -rf $(OBJS) $(TARGET)

debug: CFLAGS += -g3 -O0 -no-pie -fno-omit-frame-pointer
debug: LDFLAGS += -no-pie
debug: $(TARGET)

release: CFLAGS += -flto -fomit-frame-pointer -ffunction-sections -fdata-sections -DNDEBUG -DNO_VERBOSE
release: LDFLAGS += -s -Wl,--gc-sections
release: clean all

apt:
	sudo apt-get install libx11-dev
pacman:
	sudo pacman -S freetype2 fontconfig

install: all
	mkdir -p ~/.local/bin
	cp $(TARGET) ~/.local/bin/

uninstall:
	rm -f ~/.local/bin/snote

report:
	@cat /etc/os-release > report.txt
	@echo >> report.txt
	@uname -a >> report.txt
	@echo >> report.txt
	@test -f ~/.local/share/xorg/Xorg.0.log && head ~/.local/share/xorg/Xorg.0.log | grep -iP '(X\.Org X Server|X Protocol)' >> report.txt || (echo "No ~/.local/share/xorg/Xorg.0.log" && test -f /var/log/Xorg.0.log && head /var/log/Xorg.0.log | grep -iP '(X\.Org X Server|X Protocol)' >> report.txt || (echo 'No /var/log/Xorg.0.log' && echo 'Cannot determine xorg version'))
	@echo 'See ./report.txt'

lint:
	cppcheck -q -j8 --enable=all --check-level=exhaustive --inconclusive --suppress='*:vendor/*' --suppress=missingIncludeSystem --cppcheck-build-dir=cache src

pvs: clean pvs-init pvs-analyze pvs-html

pvs-init:
	pvs-studio-analyzer trace -- make -j8

pvs-analyze:
	pvs-studio-analyzer analyze --disableLicenseExpirationCheck

pvs-html:
	rm -rf pvs-studio-html
	plog-converter -t fullhtml -o pvs-studio-html PVS-Studio.log

.PHONY: all clean clean-all debug release apt pacman install uninstall report lint pvs pvs-init pvs-analyze pvs-html
