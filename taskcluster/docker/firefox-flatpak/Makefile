all:
	echo 'Knowing is not enough, we must apply. Willing is not enough, we must do. (Bruce Lee)'
install:
	mkdir -p $(prefix)/lib/firefox
	cp -r firefox $(prefix)/lib
	mkdir -p $(prefix)/share/appdata
	cp -p org.mozilla.Firefox.appdata.xml $(prefix)/share/appdata
	desktop-file-install --dir $(prefix)/share/applications org.mozilla.Firefox.desktop
	mkdir -p $(prefix)/share/icons/hicolor/128x128/apps/ $(prefix)/share/appdata
	cp -p firefox/browser/chrome/icons/default/default128.png $(prefix)/share/icons/hicolor/128x128/apps/org.mozilla.Firefox.png
	mkdir -p $(prefix)/lib/firefox/distribution/extensions
	cp -rp langpacks/* $(prefix)/lib/firefox/distribution/extensions
	cp distribution.ini $(prefix)/lib/firefox/distribution
	mkdir -p $(prefix)/lib/firefox/browser/defaults/preferences
	cp default-preferences.js $(prefix)/lib/firefox/browser/defaults/preferences
	# Create wrapper script which will make files downloaded for opening available outside of sandbox
	mkdir -p $(prefix)/bin
	echo '#!/bin/bash' > $(prefix)/bin/firefox
	echo 'export TMPDIR=$$XDG_CACHE_HOME/tmp' >> $(prefix)/bin/firefox
	echo '$(prefix)/lib/firefox/firefox $$*' >> $(prefix)/bin/firefox
	chmod +x $(prefix)/bin/firefox
