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

#define CVAL(p)   (*(p++))
#define SVAL(p)   ((*((p++) + 1) << 8) | CVAL(p))

#define REPEAT(statement) { while ((count > 0) && (x < width)) { statement; count--; x++; } }
#define MASK_UPDATE() { maskpix <<= 1; if (maskpix == 0) { mask = CVAL(input); maskpix = 1; } }

BOOL bitmap_decompress(unsigned char *output, int width, int height,
		       unsigned char *input, int size)
{
	unsigned char *end = input + size;
	unsigned char *prevline, *line = NULL;
	int opcode, count, offset, isfillormix, x = width;
	uint8 code, mask, maskpix, color1, color2;
	uint8 mix = 0xff;

	dump_data(input, end-input);
	while (input < end)
	{
		fprintf(stderr, "Offset %d from end\n", end-input);
		code = CVAL(input);
		opcode = code >> 4;

		/* Handle different opcode forms */
		switch (opcode)
		{
			case 0xc:
			case 0xd:
			case 0xe:
				opcode -= 6;
				count = code & 0xf;
				offset = 16;
				break;

			case 0xf:
				opcode = code & 0xf;
				count = (opcode < 13) ? SVAL(input) : 1;
				offset = 0;
				break;

			default:
				opcode >>= 1;
				count = code & 0x1f;
				offset = 32;
				break;
		}

		/* Handle strange cases for counts */
		if (offset != 0)
		{
			isfillormix = ((opcode == 2) || (opcode == 7));

			if (count == 0)
			{
				if (isfillormix)
					count = CVAL(input) + 1;
				else
					count = CVAL(input) + offset;
			}
			else if (isfillormix)
			{
				count <<= 3;
			}
		}

		/* Read preliminary data */
		maskpix = 0;
		switch (opcode)
		{
			case 3: /* Color */
				color1 = CVAL(input);
			case 8: /* Bicolor */
				color2 = CVAL(input);
				break;
			case 6: /* SetMix/Mix */
			case 7: /* SetMix/FillOrMix */
				mix = CVAL(input);
				opcode -= 5;
				break;
		}

		/* Output body */
		while (count > 0)
		{
			if (x >= width)
			{
				if (height <= 0)
					return True;

				x = 0;
				height--;

				prevline = line;
				line = output + height * width;
			}

			switch (opcode)
			{
				case 0: /* Fill */
					fprintf(stderr, "Fill %d\n", count);
					if (prevline == NULL)
						REPEAT(line[x] = 0)
					else
						REPEAT(line[x] = prevline[x])
					break;

				case 1: /* Mix */
					fprintf(stderr, "Mix %d\n", count);
					if (prevline == NULL)
						REPEAT(line[x] = mix)
					else
						REPEAT(line[x] = prevline[x] ^ mix)
					break;

#if 0
				case 2: /* Fill or Mix */
					REPEAT(line[x] = 0);
					break;
					if (prevline == NULL)
					    REPEAT( 
						   MASK_UPDATE();

						   if (mask & maskpix)
							line[x] = mix;
						   else
							line[x] = 0;
					    )
					else
					    REPEAT(
						   MASK_UPDATE();

						   if (mask & maskpix)
							line[x] = prevline[x] ^ mix;
						   else
							line[x] = prevline[x];
					    )
					break;
#endif

				case 3: /* Colour */
					fprintf(stderr, "Colour %d\n", count);
					REPEAT(line[x] = color2)
					break;

				case 4: /* Copy */
					fprintf(stderr, "Copy %d\n", count);
					REPEAT(line[x] = CVAL(input))
					break;

#if 0
				case 8: /* Bicolor */
					REPEAT(line[x] = color1; line[++x] = color2)
					break;

				case 13: /* White */
					REPEAT(line[x] = 0xff)
					break;

				case 14: /* Black */
					REPEAT(line[x] = 0x00)
					break;
#endif

				default:
					fprintf(stderr, "Unknown bitmap opcode 0x%x\n", opcode);
					return False;
			}
		}
	}

	return True;
}
