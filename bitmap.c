/*
   rdesktop: A Remote Desktop Protocol client.
   Bitmap decompression routines
   Copyright (C) Matthew Chapman 1999-2000
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "includes.h"
#include <fcntl.h>

#define BITMAP_DEBUG 1

#if BITMAP_DEBUG
void hexdump(char *filename, unsigned char *data, int length);
#endif

#define RCVAL()   (*(input++))
#define RSVAL()   ((*((input++) + 1) << 8) | RCVAL())
#define SCVAL(v)  {*(output++) = (v);}

#define FILL()    {while (n-- > 0) { if (output - start < width) { SCVAL(0) } else { SCVAL(*(output-width)); }}}
#define MIX()     {while (n-- > 0) { if (output - start < width) { SCVAL(mix) } else { SCVAL(*(output-width) ^ mix); }}}
#define COPY()    {while (n-- > 0) { SCVAL(RCVAL()); }}
#define COLOR()   {int color = RCVAL(); \
                   while (n-- > 0) { SCVAL(color); }}
#define BICOLOR() {int color1 = RCVAL(); int color2 = RCVAL(); \
                   while (n-- > 0) { SCVAL(color1); SCVAL(color2); }}
#define SETMIX_MIX() {mix = RCVAL(); MIX();}
#define COPY_PACKED() {n++; n/=2; while (n-- > 0) \
                      {unsigned char c = RCVAL(); SCVAL((c & 0xF0) >> 4); \
                                                  SCVAL(c & 0x0F); }}

BOOL bitmap_decompress(unsigned char *input, int size,
		       unsigned char *output, int width)
{
	unsigned char *savedinput = input;
	unsigned char *start = output;
	unsigned char *end = input + size;
	unsigned char code;
	unsigned char mix = 0xFF;
	int n, savedn;

	while (input < end)
	{
		code = RCVAL();
		switch (code)
		{
		case 0x00: // Fill
			n = RCVAL() + 32;
			FILL();
			break;
		case 0xF0: // Fill
			n = RSVAL();
			FILL();
			break;
		case 0x20: // Mix
			n = RCVAL() + 32;
			MIX();
			break;
		case 0xF1: // Mix
			n = RSVAL();
			MIX();
			break;
		case 0x40: // FillOrMix
			fprintf(stderr, "FillOrMix unsupported\n");
			savedn = n = RCVAL() + 1;
			MIX();
			input += (savedn+7)/8;
			break;
		case 0xF2:
			fprintf(stderr, "FillOrMix unsupported\n");
			savedn = n = RSVAL();
			MIX();
			input += (savedn+7)/8;
			break;
		case 0x60: // Color
			n = RCVAL() + 32;
			COLOR();
			break;
		case 0xF3:
			n = RSVAL();
			fprintf(stderr, "Color %d\n", n);
			COLOR();
			break;
		case 0x80: // Copy
			n = RCVAL() + 32;
			COPY();
			break;
		case 0xF4:
			n = RSVAL();
			COPY();
			break;
		case 0xA0: // Copy Packed
			fprintf(stderr, "CopyPacked 1\n");
			n = RCVAL() + 32;
			COPY_PACKED();
			break;
		case 0xF5:
			fprintf(stderr, "CopyPacked 2\n");
			n = RSVAL();
			COPY_PACKED();
			break;
		case 0xC0: // SetMix_Mix
			fprintf(stderr, "SetMix_Mix 1\n");
			n = RCVAL() + 16;
			SETMIX_MIX();
			break;
		case 0xF6:
			fprintf(stderr, "SetMix_Mix 2\n");
			n = RSVAL();
			SETMIX_MIX();
			break;
		case 0xD0: // SetMix_FillOrMix
			fprintf(stderr, "SetMix_FillOrMix unsupported\n");
			savedn = n = RCVAL() + 1;
			SETMIX_MIX();
			input += (savedn+7)/8;
			break;
		case 0xF7:
			fprintf(stderr, "SetMix_FillOrMix unsupported\n");
			savedn = n = RSVAL();
			SETMIX_MIX();
			input += (savedn+7)/8;
			break;
		case 0xE0: // Bicolor
			fprintf(stderr, "Bicolor 1\n");
			n = RCVAL() + 16;
			BICOLOR();
			break;
		case 0xF8:
			fprintf(stderr, "Bicolor 2\n");
			n = RSVAL();
			BICOLOR();
			break;
		case 0xF9: // FillOrMix_1
			fprintf(stderr, "FillOrMix_1 unsupported\n");
			return False;
		case 0xFA: // FillOrMix_2
			fprintf(stderr, "FillOrMix_2 unsupported\n");
			return False;
		case 0xFD: // White
			SCVAL(0xFF);
			break;
		case 0xFE: // Black
			SCVAL(0);
			break;
		default:
			n = code & 31;

			if (n == 0)
			{
				fprintf(stderr, "Undefined escape 0x%X\n", code);
				return False;
			}

			switch ((code >> 5) & 7)
			{
			case 0: // Fill
				FILL();
				break;
			case 1: // Mix
				MIX();
				break;
			case 2: // FillOrMix
				fprintf(stderr, "FillOrMix unsupported\n");
				n *= 8;
				savedn = n;
				MIX();
				input += (savedn+7)/8;
				break;
			case 3: // Color
				COLOR();
				break;
			case 4: // Copy
				COPY();
				break;
			case 5: // Copy Packed
				fprintf(stderr, "CopyPacked 3\n");
				COPY_PACKED();
				break;
			case 6:
				n = code & 15;

				switch ((code >> 4) & 15)
				{
				case 0xC:
					fprintf(stderr, "SetMix_Mix 3\n");
					SETMIX_MIX();
					break;
				case 0xD:
					fprintf(stderr, "SetMix_FillOrMix unsupported\n");
					n *= 8;
					savedn = n;
					SETMIX_MIX();
					input += (savedn+7)/8;
					break;
				case 0xE:
					fprintf(stderr, "Bicolor 3\n");
					BICOLOR();
					break;
				default:
					fprintf(stderr, "Undefined escape 0x%X\n", code);
					return False;
				}
			}
		}
	}

	printf("Uncompressed size: %d\n", output - start);
#if BITMAP_DEBUG
	{
		static int bmpno = 1;
		char filename[64];

		snprintf(filename, sizeof(filename)-1, "in%d.raw", bmpno);
		hexdump(filename, savedinput, size);

		snprintf(filename, sizeof(filename)-1, "out%d.raw", bmpno);
		hexdump(filename, start, output-start);

		bmpno++;
	}
#endif

	return True;
}


#if BITMAP_DEBUG
void hexdump(char *filename, unsigned char *data, int length)
{
	/*
	int i;

	for (i = 0; i < length; i++)
	{
		printf("%02X ", data[i]);

		if (i % 16 == 15)
			printf("\n");
	}
	*/

	int fd;

	fd = open(filename, O_WRONLY|O_CREAT, 0600);
	write(fd, data, length);
	close(fd);
}
#endif
