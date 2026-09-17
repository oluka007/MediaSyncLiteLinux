SUBDIRS=src
RESOURCES_PATH=share/ui/*
DESKTOP_FILE=share/applications/mediasynclite.desktop
ICON_SIZES=16x16 22x22 24x24 32x32 48x48 64x64 128x128 256x256

# PREFIX is environment variable, but if it is not set, then set default value
ifeq ($(PREFIX),)
    PREFIX := /usr
endif

default: all

all:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE)); done

.PHONY: clean install

clean:
	@for dir in $(SUBDIRS); do (cd $$dir; $(MAKE) clean); done

install:
	install -s -D mediasynclite $(DESTDIR)$(PREFIX)/bin/mediasynclite
	@for file in $(RESOURCES_PATH); do (echo "Install: " $$file "to:" $(DESTDIR)$(PREFIX)/share/mediasynclite/`basename $$file`; if [ -f $$file ]; then install -m 644 -D $$file $(DESTDIR)$(PREFIX)/share/mediasynclite/`basename $$file`; fi); done
	install -m 644 -D $(DESKTOP_FILE) $(DESTDIR)$(PREFIX)/share/applications/mediasynclite.desktop
	@for size in $(ICON_SIZES); do \
		icon=share/icons/hicolor/$$size/apps/mediasynclite.png; \
		if [ -f $$icon ]; then \
			echo "Install: " $$icon "to:" $(DESTDIR)$(PREFIX)/share/icons/hicolor/$$size/apps/mediasynclite.png; \
			install -m 644 -D $$icon $(DESTDIR)$(PREFIX)/share/icons/hicolor/$$size/apps/mediasynclite.png; \
		fi; \
	done
	@if [ -z "$(DESTDIR)" ]; then \
		command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q $(PREFIX)/share/applications || true; \
		command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f $(PREFIX)/share/icons/hicolor || true; \
	fi
