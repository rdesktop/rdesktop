CC=gcc
CFLAGS=-fPIC -Wall -Wextra -ggdb -gdwarf-2 -g3
CGREEN_RUNNER=cgreen-runner

TESTS=resize rdp xwin utils parse_geometry mcs asn


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

PARSE_MOCKS=ui_mock.o rdpdr_mock.o rdpedisp_mock.o ssl_mock.o ctrl_mock.o secure_mock.o \
	tcp_mock.o dvc_mock.o rdp_mock.o cache_mock.o cliprdr_mock.o disk_mock.o lspci_mock.o \
	parallel_mock.o printer_mock.o serial_mock.o xkeymap_mock.o utils_mock.o xwin_mock.o

MCS_MOCKS=utils_mock.o secure_mock.o iso_mock.o

ASN_MOCKS=utils_mock.o

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

parse_geometry: parse_geometry_test.o $(PARSE_MOCKS) ../rdesktop.c
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ parse_geometry_test.o $(PARSE_MOCKS)

mcs: mcs_test.o $(MCS_MOCKS) stream.o
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^

asn: asn_test.o $(ASN_MOCKS) asn.o stream.o
	$(CC) $(CFLAGS) -shared -lcgreen -o $@ $^

asn.o: ../asn.c
	$(CC) $(CFLAGS) -c -o $@ $^

stream.o: ../stream.c
	$(CC) $(CFLAGS) -c -o $@ $^

.PHONY: clean
clean:
	rm -f $(TESTS) *_mock.o *_test.o
