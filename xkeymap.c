/*
   rdesktop: A Remote Desktop Protocol client.
   User interface services - X keyboard mapping
   Copyright (C) Matthew Chapman 1999-2001
   
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

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "rdesktop.h"

#define KEYMAP_SIZE 4096
#define KEYMAP_MASK (KEYMAP_SIZE - 1)

extern Display *display;
extern char keymapname[16];
extern int keylayout;

static uint8 keymap[KEYMAP_SIZE];
static unsigned int min_keycode;

static BOOL
xkeymap_read(char *mapname)
{
	FILE *fp;
	char line[PATH_MAX], path[PATH_MAX];
	char *keyname, *p;
	KeySym keysym;
	unsigned char keycode;

	strcpy(path, KEYMAP_PATH);
	strncat(path, mapname, sizeof(path) - sizeof(KEYMAP_PATH));

	fp = fopen(path, "r");
	if (fp == NULL)
	{
		error("Failed to open keymap %s\n", path);
		return False;
	}

	while (fgets(line, sizeof(line), fp) != NULL)
	{
		p = strchr(line, '\n');
		if (p != NULL)
			*p = 0;

		keycode = strtol(line, &keyname, 16);
		if ((keycode != 0) && (*keyname == ' '))
		{
			do
			{
				keyname++;
				p = strchr(keyname, ' ');
				if (p != NULL)
					*p = 0;

				keysym = XStringToKeysym(keyname);
				if (keysym == NoSymbol)
					error("Bad keysym %s in keymap %s\n",
					      keyname, mapname);

				keymap[keysym & KEYMAP_MASK] = keycode;
				keyname = p;

			}
			while (keyname != NULL);
		}
		else if (strncmp(line, "include ", 8) == 0)
		{
			if (!xkeymap_read(line + 8))
				return False;
		}
		else if (strncmp(line, "map ", 4) == 0)
		{
			keylayout = strtol(line + 4, NULL, 16);
		}
		else if (line[0] != '#')
		{
			error("Malformed line in keymap %s\n", mapname);
		}
	}

	fclose(fp);
	return True;
}

void
xkeymap_init(void)
{
	unsigned int max_keycode;

	XDisplayKeycodes(display, &min_keycode, &max_keycode);

	if (strcmp(keymapname, "none"))
		xkeymap_read(keymapname);
}

uint8
xkeymap_translate_key(unsigned int keysym, unsigned int keycode,
		      uint16 * flags)
{
	uint8 scancode;

	scancode = keymap[keysym & KEYMAP_MASK];
	if (scancode != 0)
	{
		if (scancode & 0x80)
			*flags |= KBD_FLAG_EXT;

		return (scancode & 0x7f);
	}

	/* not in keymap, try to interpret the raw scancode */

	if ((keycode >= min_keycode) && (keycode <= 0x60))
		return (uint8) (keycode - min_keycode);

	*flags |= KBD_FLAG_EXT;

	switch (keycode)
	{
		case 0x61:	/* home */
			return 0x47;
		case 0x62:	/* up arrow */
			return 0x48;
		case 0x63:	/* page up */
			return 0x49;
		case 0x64:	/* left arrow */
			return 0x4b;
		case 0x66:	/* right arrow */
			return 0x4d;
		case 0x67:	/* end */
			return 0x4f;
		case 0x68:	/* down arrow */
			return 0x50;
		case 0x69:	/* page down */
			return 0x51;
		case 0x6a:	/* insert */
			return 0x52;
		case 0x6b:	/* delete */
			return 0x53;
		case 0x6c:	/* keypad enter */
			return 0x1c;
		case 0x6d:	/* right ctrl */
			return 0x1d;
		case 0x6f:	/* ctrl - print screen */
			return 0x37;
		case 0x70:	/* keypad '/' */
			return 0x35;
		case 0x71:	/* right alt */
			return 0x38;
		case 0x72:	/* ctrl break */
			return 0x46;
		case 0x73:	/* left window key */
			return 0x5b;
		case 0x74:	/* right window key */
			return 0x5c;
		case 0x75:	/* menu key */
			return 0x5d;
	}

	return 0;
}

uint16
xkeymap_translate_button(unsigned int button)
{
	switch (button)
	{
		case Button1:	/* left */
			return MOUSE_FLAG_BUTTON1;
		case Button2:	/* middle */
			return MOUSE_FLAG_BUTTON3;
		case Button3:	/* right */
			return MOUSE_FLAG_BUTTON2;
		case Button4:	/* wheel up */
			return MOUSE_FLAG_BUTTON4;
		case Button5:	/* wheel down */
			return MOUSE_FLAG_BUTTON5;
	}

	return 0;
}
