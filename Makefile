# SPDX-License-Identifier: Apache-2.0
CC ?= cc
CFLAGS ?= -O1 -g
WARN = -std=c99 -Wall -Wextra -Werror -UNDEBUG
.PHONY: check sanitize
check: build/test_core
	./build/test_core
build/test_core: src/ipheth.c tests/test_core.c include/ipheth.h
	mkdir -p build
	$(CC) $(CFLAGS) $(WARN) -Iinclude src/ipheth.c tests/test_core.c -o $@
sanitize:
	mkdir -p build-san
	$(CC) -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude src/ipheth.c tests/test_core.c -o build-san/test_core
	ASAN_OPTIONS=detect_leaks=1 ./build-san/test_core

# Point at an external CherryUSB checkout; no CherryUSB source is vendored.
CHERRYUSB_ROOT ?= ../CherryUSB
USB_INC = -Itests/config -I$(CHERRYUSB_ROOT)/core -I$(CHERRYUSB_ROOT)/common -I$(CHERRYUSB_ROOT)/class/hub
CLASS_SRC = src/ipheth.c port/cherryusb/usbh_ipheth.c tests/test_class.c
.PHONY: check-class sanitize-class
check-class:
	mkdir -p build
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -UNDEBUG -pthread -DIPHETH_EXTERNAL_NET_ADAPTER -Iinclude $(USB_INC) $(CLASS_SRC) -o build/test_class
	./build/test_class
sanitize-class:
	mkdir -p build-san
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -UNDEBUG -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -DIPHETH_EXTERNAL_NET_ADAPTER -Iinclude $(USB_INC) $(CLASS_SRC) -o build-san/test_class
	ASAN_OPTIONS=detect_leaks=1 ./build-san/test_class
