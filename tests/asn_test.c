#include <cgreen/cgreen.h>
#include <cgreen/mocks.h>
#include "../rdesktop.h"

char g_codepage[16];

/* Boilerplate */
Describe(ASN1);
BeforeEach(ASN1) {}
AfterEach(ASN1) {}

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

Ensure(ASN1, can_create_empty_sequence)
{
  struct stream *s, *empty;
  uint8_t expected_data[] = {0x30, 0x00};

  s = s_alloc(100);
  empty = s_alloc(100);

  ber_out_sequence(s, empty);
  s_mark_end(s);

  assert_that(s_length(s), is_equal_to(sizeof(expected_data)));
  assert_that(s->data, is_equal_to_contents_of(expected_data, sizeof(expected_data)));
}

Ensure(ASN1, can_create_empty_sequence_using_null)
{
  struct stream *s;
  uint8_t expected_data[] = {0x30, 0x00};

  s = s_alloc(100);

  ber_out_sequence(s, NULL);
  s_mark_end(s);

  assert_that(s_length(s), is_equal_to(sizeof(expected_data)));
  assert_that(s->data, is_equal_to_contents_of(expected_data, sizeof(expected_data)));
}

Ensure(ASN1, can_create_sequence_of_two_integers)
{
  struct stream *s, *content;
  uint8_t expected_data[] = {0x30, 0x08, 0x02, 0x02, 0x00, 0xbe, 0x02, 0x02, 0x00, 0xef};

  s = s_alloc(100);
  content = s_alloc(100);

  ber_out_integer(content, 0xbe);
  ber_out_integer(content, 0xef);
  s_mark_end(content);

  ber_out_sequence(s, content);
  s_mark_end(s);

  assert_that(s_length(s), is_equal_to(sizeof(expected_data)));
  assert_that(s->data, is_equal_to_contents_of(expected_data, sizeof(expected_data)));
}

Ensure(ASN1, can_create_sequence_of_one_integer)
{
  struct stream *s, *content;
  uint8_t expected_data[] = {0x30, 0x04, 0x02, 0x02, 0x00, 0xbe};

  s = s_alloc(100);
  content = s_alloc(100);

  ber_out_integer(content, 0xbe);
  s_mark_end(content);

  ber_out_sequence(s, content);
  s_mark_end(s);

  assert_that(s_length(s), is_equal_to(sizeof(expected_data)));
  assert_that(s->data, is_equal_to_contents_of(expected_data, sizeof(expected_data)));
}
