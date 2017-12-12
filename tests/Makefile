CC=gcc
CFLAGS=-fPIC -Wall -Wextra -ggdb -gdwarf-2 -g3
CGREEN_RUNNER=cgreen-runner

TESTS=resize rdp xwin utils


RDP_MOCKS=ui_mock.o bitmap_mock.o secure_mock.o ssl_mock.o mppc_mock.o \
	cache_mock.o pstcache_mock.o orders_mock.o rdesktop_mock.o \
	rdp5_mock.o xkeymap_mock.o tcp_mock.o

XWIN_MOCKS=x11_mock.o cache_mock.o xclip_mock.o xkeymap_mock.o seamless_mock.o \
	ctrl_mock.o rdpdr_mock.o ewmh_mock.o rdpedisp_mock.o rdp_mock.o

UTILS_MOCKS=

RESIZE_MOCKS=x11_mock.o cache_mock.o xclip_mock.o xkeymap_mock.o seamless_mock.o \
	ctrl_mock.o rdpdr_mock.o ewmh_mock.o rdpedisp_mock.o bitmap_mock.o \
	ssl_mock.o mppc_mock.o pstcache_mock.o orders_mock.o rdesktop_mock.o rdp5_mock.o \
	tcp_mock.o licence_mock.o mcs_mock.o channels_mock.o


all: test

.PHONY: test
test: $(foreach test, $(TESTS), runtest.$(test))


.PHONY: runtest.%
runtest.%: %
	$(CGREEN_RUNNER) $^


rdp: rdp_test.o $(RDP_MOCKS)
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^

xwin: xwin_test.o $(XWIN_MOCKS)
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^ -lX11 -lXcursor

utils: utils_test.o $(UTILS_MOCKS)
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^

resize: resize_test.o $(RESIZE_MOCKS)
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^ -lX11 -lXcursor


.PHONY: clean
clean:
	rm -f $(TESTS) *_mock.o *_test.o
