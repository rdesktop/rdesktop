/*
   rdesktop: A Remote Desktop Protocol client.
   Bitmap decompression routines
   Copyright (C) Matthew Chapman 1999-2002

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

#include "rdesktop.h"

#define CVAL(p)   (*(p++))

static uint32
cvalx(unsigned char **input, int Bpp)
{
	uint32 rv = 0;
	memcpy(&rv, *input, Bpp);
	*input += Bpp;
	return rv;
}

static void
setli(unsigned char *input, int offset, uint32 value, int Bpp)
{
	input += offset * Bpp;
	memcpy(input, &value, Bpp);
}

static uint32
getli(unsigned char *input, int offset, int Bpp)
{
	uint32 rv = 0;
	input += offset * Bpp;
	memcpy(&rv, input, Bpp);
	return rv;
}

#define UNROLL8(exp) { exp exp exp exp exp exp exp exp }

#define REPEAT(statement) \
{ \
	while((count & ~0x7) && ((x+8) < width)) \
		UNROLL8( statement; count--; x++; ); \
	\
	while((count > 0) && (x < width)) { statement; count--; x++; } \
}

#define MASK_UPDATE() \
{ \
	mixmask <<= 1; \
	if (mixmask == 0) \
	{ \
		mask = fom_mask ? fom_mask : CVAL(input); \
		mixmask = 1; \
	} \
}

BOOL
bitmap_decompress(unsigned char *output, int width, int height, unsigned char *input, int size,
		  int Bpp)
{
	unsigned char *end = input + size;
	unsigned char *prevline = NULL, *line = NULL;
	int opcode, count, offset, isfillormix, x = width;
	int lastopcode = -1, insertmix = False, bicolour = False;
	uint8 code;
	uint32 colour1 = 0, colour2 = 0;
	uint8 mixmask, mask = 0;
	uint32 mix = 0xffffffff;
	int fom_mask = 0;

	while (input < end)
	{
		fom_mask = 0;
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
				if (opcode < 9)
				{
					count = CVAL(input);
					count |= CVAL(input) << 8;
				}
				else
				{
					count = (opcode < 0xb) ? 8 : 1;
				}
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
		switch (opcode)
		{
			case 0:	/* Fill */
				if ((lastopcode == opcode) && !((x == width) && (prevline == NULL)))
					insertmix = True;
				break;
			case 8:	/* Bicolour */
				colour1 = cvalx(&input, Bpp);
			case 3:	/* Colour */
				colour2 = cvalx(&input, Bpp);
				break;
			case 6:	/* SetMix/Mix */
			case 7:	/* SetMix/FillOrMix */
				mix = cvalx(&input, Bpp);
				opcode -= 5;
				break;
			case 9:	/* FillOrMix_1 */
				mask = 0x03;
				opcode = 0x02;
				fom_mask = 3;
				break;
			case 0x0a:	/* FillOrMix_2 */
				mask = 0x05;
				opcode = 0x02;
				fom_mask = 5;
				break;

		}

		lastopcode = opcode;
		mixmask = 0;

		/* Output body */
		while (count > 0)
		{
			if (x >= width)
			{
				if (height <= 0)
					return False;

				x = 0;
				height--;

				prevline = line;
				line = output + height * width * Bpp;
			}

			switch (opcode)
			{
				case 0:	/* Fill */
					if (insertmix)
					{
						if (prevline == NULL)
							setli(line, x, mix, Bpp);
						else
							setli(line, x,
							      getli(prevline, x, Bpp) ^ mix, Bpp);

						insertmix = False;
						count--;
						x++;
					}

					if (prevline == NULL)
					{
					REPEAT(setli(line, x, 0, Bpp))}
					else
					{
						REPEAT(setli
						       (line, x, getli(prevline, x, Bpp), Bpp));
					}
					break;

				case 1:	/* Mix */
					if (prevline == NULL)
					{
						REPEAT(setli(line, x, mix, Bpp));
					}
					else
					{
						REPEAT(setli
						       (line, x, getli(prevline, x, Bpp) ^ mix,
							Bpp));
					}
					break;

				case 2:	/* Fill or Mix */
					if (prevline == NULL)
					{
						REPEAT(MASK_UPDATE();
						       if (mask & mixmask) setli(line, x, mix, Bpp);
						       else
						       setli(line, x, 0, Bpp););
					}
					else
					{
						REPEAT(MASK_UPDATE();
						       if (mask & mixmask)
						       setli(line, x, getli(prevline, x, Bpp) ^ mix,
							     Bpp);
						       else
						       setli(line, x, getli(prevline, x, Bpp),
							     Bpp););
					}
					break;

				case 3:	/* Colour */
					REPEAT(setli(line, x, colour2, Bpp));
					break;

				case 4:	/* Copy */
					REPEAT(setli(line, x, cvalx(&input, Bpp), Bpp));
					break;

				case 8:	/* Bicolour */
					REPEAT(if (bicolour)
					       {
					       setli(line, x, colour2, Bpp); bicolour = False;}
					       else
					       {
					       setli(line, x, colour1, Bpp); bicolour = True;
					       count++;}
					);
					break;

				case 0xd:	/* White */
					REPEAT(setli(line, x, 0xffffffff, Bpp));
					break;

				case 0xe:	/* Black */
					REPEAT(setli(line, x, 0, Bpp));
					break;

				default:
					unimpl("bitmap opcode 0x%x\n", opcode);
					return False;
			}
		}
	}

	return True;
}
