#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"

/* Boilerplate */
Describe(MCS);
BeforeEach(MCS) {};
AfterEach(MCS) {};

char g_codepage[16];
VCHANNEL g_channels[1];
unsigned int g_num_channels;

#include "../asn.c"
#include "../mcs.c"
#include "../stream.h"

/* malloc; exit if out of memory */
void *
xmalloc(int size)
{
	void *mem = malloc(size);
	if (mem == NULL)
	{
		logger(Core, Error, "xmalloc, failed to allocate %d bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* realloc; exit if out of memory */
void *
xrealloc(void *oldmem, size_t size)
{
	void *mem;

	if (size == 0)
		size = 1;
	mem = realloc(oldmem, size);
	if (mem == NULL)
	{
		logger(Core, Error, "xrealloc, failed to reallocate %ld bytes", size);
		exit(EX_UNAVAILABLE);
	}
	return mem;
}

/* free */
void
xfree(void *mem)
{
	free(mem);
}


/* Test function */
Ensure(MCS, should_produce_valid_packet_for_McsSendCJrq)
{
  uint16 chan_id;
  uint8_t content[] = {0x38, 0x00, 0x2A, 0x00, 0x0D};

  struct stream *s;
  s = s_alloc(5);

  chan_id = 13;
  g_mcs_userid = 42;

  expect(logger);
  expect(iso_init, will_return(s));
  expect(iso_send, when(stream->data, is_equal_to_contents_of(content, sizeof(content))));

  mcs_send_cjrq(chan_id);
  s_free(s);
}

/* Test function */
Ensure(MCS, should_produce_valid_packet_for_McsSendDPU)
{
  int reason = 1;
  struct stream *s;
  uint8_t content[] = {0x30, 0x06, 0x02, 0x02, 0x00, reason, 0x30, 0x00};

  s = s_alloc(8);

  expect(logger);
  expect(iso_init, will_return(s));

  expect(iso_send, when(stream->data, is_equal_to_contents_of(content, sizeof(content))));

  mcs_send_dpu(reason);
  s_free(s);
}
