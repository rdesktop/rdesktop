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
#include <ctype.h>
#include <limits.h>
#include "rdesktop.h"
#include "scancodes.h"

#define KEYMAP_SIZE 4096
#define KEYMAP_MASK (KEYMAP_SIZE - 1)
#define KEYMAP_MAX_LINE_LENGTH 80

extern Display *display;
extern char keymapname[16];
extern int keylayout;
extern BOOL enable_compose;

static key_translation keymap[KEYMAP_SIZE];
static int min_keycode;
static uint16 remote_modifier_state = 0;

static void
add_to_keymap(char *keyname, uint8 scancode, uint16 modifiers, char *mapname)
{
	KeySym keysym;

	keysym = XStringToKeysym(keyname);
	if (keysym == NoSymbol)
	{
		error("Bad keysym %s in keymap %s\n", keyname, mapname);
		return;
	}

	DEBUG_KBD(("Adding translation, keysym=0x%x, scancode=0x%x, "
		   "modifiers=0x%x\n", (unsigned int) keysym, scancode, modifiers));

	keymap[keysym & KEYMAP_MASK].scancode = scancode;
	keymap[keysym & KEYMAP_MASK].modifiers = modifiers;

	return;
}


static BOOL
xkeymap_read(char *mapname)
{
	FILE *fp;
	char line[KEYMAP_MAX_LINE_LENGTH], path[PATH_MAX];
	unsigned int line_num = 0;
	unsigned int line_length = 0;
	char *keyname, *p;
	char *line_rest;
	uint8 scancode;
	uint16 modifiers;


	strcpy(path, KEYMAP_PATH);
	strncat(path, mapname, sizeof(path) - sizeof(KEYMAP_PATH));

	fp = fopen(path, "r");
	if (fp == NULL)
	{
		error("Failed to open keymap %s\n", path);
		return False;
	}

	/* FIXME: More tolerant on white space */
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		line_num++;

		/* Replace the \n with \0 */
		p = strchr(line, '\n');
		if (p != NULL)
			*p = 0;

		line_length = strlen(line);

		/* Completely empty line */
		if (strspn(line, " \t\n\r\f\v") == line_length)
		{
			continue;
		}

		/* Include */
		if (strncmp(line, "include ", 8) == 0)
		{
			if (!xkeymap_read(line + 8))
				return False;
			continue;
		}

		/* map */
		if (strncmp(line, "map ", 4) == 0)
		{
			keylayout = strtol(line + 4, NULL, 16);
			DEBUG_KBD(("Keylayout 0x%x\n", keylayout));
			continue;
		}

		/* compose */
		if (strncmp(line, "enable_compose", 15) == 0)
		{
			DEBUG_KBD(("Enabling compose handling\n"));
			enable_compose = True;
			continue;
		}

		/* Comment */
		if (line[0] == '#')
		{
			continue;
		}

		/* Normal line */
		keyname = line;
		p = strchr(line, ' ');
		if (p == NULL)
		{
			error("Bad line %d in keymap %s\n", line_num, mapname);
			continue;
		}
		else
		{
			*p = 0;
		}

		/* scancode */
		p++;
		scancode = strtol(p, &line_rest, 16);

		/* flags */
		/* FIXME: Should allow case-insensitive flag names. 
		   Fix by using lex+yacc... */
		modifiers = 0;
		if (strstr(line_rest, "altgr"))
		{
			MASK_ADD_BITS(modifiers, MapAltGrMask);
		}

		if (strstr(line_rest, "shift"))
		{
			MASK_ADD_BITS(modifiers, MapLeftShiftMask);
		}

		if (strstr(line_rest, "numlock"))
		{
			MASK_ADD_BITS(modifiers, MapNumLockMask);
		}

		if (strstr(line_rest, "localstate"))
		{
			MASK_ADD_BITS(modifiers, MapLocalStateMask);
		}

		add_to_keymap(keyname, scancode, modifiers, mapname);

		if (strstr(line_rest, "addupper"))
		{
			/* Automatically add uppercase key, with same modifiers 
			   plus shift */
			for (p = keyname; *p; p++)
				*p = toupper(*p);
			MASK_ADD_BITS(modifiers, MapLeftShiftMask);
			add_to_keymap(keyname, scancode, modifiers, mapname);
		}
	}

	fclose(fp);
	return True;
}


/* Before connecting and creating UI */
void
xkeymap_init1(void)
{
	int i;

	/* Zeroing keymap */
	for (i = 0; i < KEYMAP_SIZE; i++)
	{
		keymap[i].scancode = 0;
		keymap[i].modifiers = 0;
	}

	if (strcmp(keymapname, "none"))
	{
		xkeymap_read(keymapname);
	}

}

/* After connecting and creating UI */
void
xkeymap_init2(void)
{
	unsigned int max_keycode;
	XDisplayKeycodes(display, &min_keycode, (int *) &max_keycode);
}


key_translation
xkeymap_translate_key(KeySym keysym, unsigned int keycode, unsigned int state)
{
	key_translation tr = { 0, 0 };

	tr = keymap[keysym & KEYMAP_MASK];

	if (tr.modifiers & MapLocalStateMask)
	{
		/* The modifiers to send for this key should be obtained
		   from the local state. Currently, only shift is implemented. */
		if (state & ShiftMask)
		{
			tr.modifiers = MapLeftShiftMask;
		}
	}

	if (tr.scancode != 0)
	{
		DEBUG_KBD
			(("Found key translation, scancode=0x%x, modifiers=0x%x\n",
			  tr.scancode, tr.modifiers));
		return tr;
	}

	fprintf(stderr, "No translation for (keysym 0x%lx, %s)\n", keysym, get_ksname(keysym));

	/* not in keymap, try to interpret the raw scancode */
	if ((keycode >= min_keycode) && (keycode <= 0x60))
	{
		tr.scancode = keycode - min_keycode;
		fprintf(stderr, "Sending guessed scancode 0x%x\n", tr.scancode);
	}
	else
	{
		fprintf(stderr, "No good guess for keycode 0x%x found\n", keycode);
	}

	return tr;
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

char *
get_ksname(KeySym keysym)
{
	char *ksname = NULL;

	if (keysym == NoSymbol)
		ksname = "NoSymbol";
	else if (!(ksname = XKeysymToString(keysym)))
		ksname = "(no name)";

	return ksname;
}

BOOL
inhibit_key(KeySym keysym)
{
	switch (keysym)
	{
		case XK_Caps_Lock:
			return True;
			break;
		case XK_Multi_key:
			return True;
			break;
		default:
			break;
	}
	return False;
}

void
ensure_remote_modifiers(uint32 ev_time, key_translation tr)
{
	/* If this key is a modifier, do nothing */
	switch (tr.scancode)
	{
		case SCANCODE_CHAR_LSHIFT:
		case SCANCODE_CHAR_RSHIFT:
		case SCANCODE_CHAR_LCTRL:
		case SCANCODE_CHAR_RCTRL:
		case SCANCODE_CHAR_LALT:
		case SCANCODE_CHAR_RALT:
		case SCANCODE_CHAR_LWIN:
		case SCANCODE_CHAR_RWIN:
		case SCANCODE_CHAR_NUMLOCK:
			return;
		default:
			break;
	}

	/* Shift */
	if (MASK_HAS_BITS(tr.modifiers, MapShiftMask)
	    != MASK_HAS_BITS(remote_modifier_state, MapShiftMask))
	{
		/* The remote modifier state is not correct */
		if (MASK_HAS_BITS(tr.modifiers, MapShiftMask))
		{
			/* Needs this modifier. Send down. */
			rdp_send_scancode(ev_time, RDP_KEYPRESS, SCANCODE_CHAR_LSHIFT);
		}
		else
		{
			/* Should not use this modifier. Send up. */
			rdp_send_scancode(ev_time, RDP_KEYRELEASE, SCANCODE_CHAR_LSHIFT);
		}
	}

	/* AltGr */
	if (MASK_HAS_BITS(tr.modifiers, MapAltGrMask)
	    != MASK_HAS_BITS(remote_modifier_state, MapAltGrMask))
	{
		/* The remote modifier state is not correct */
		if (MASK_HAS_BITS(tr.modifiers, MapAltGrMask))
		{
			/* Needs this modifier. Send down. */
			rdp_send_scancode(ev_time, RDP_KEYPRESS, SCANCODE_CHAR_RALT);
		}
		else
		{
			/* Should not use this modifier. Send up. */
			rdp_send_scancode(ev_time, RDP_KEYRELEASE, SCANCODE_CHAR_RALT);
		}
	}

	/* NumLock */
	if (MASK_HAS_BITS(tr.modifiers, MapNumLockMask)
	    != MASK_HAS_BITS(remote_modifier_state, MapNumLockMask))
	{
		/* The remote modifier state is not correct */
		DEBUG_KBD(("Remote NumLock state is incorrect. Toggling\n"));
		if (MASK_HAS_BITS(tr.modifiers, MapNumLockMask))
		{
			/* Needs this modifier. Toggle */
			rdp_send_scancode(ev_time, RDP_KEYPRESS, SCANCODE_CHAR_NUMLOCK);
			rdp_send_scancode(ev_time, RDP_KEYRELEASE, SCANCODE_CHAR_NUMLOCK);
		}
		else
		{
			/* Should not use this modifier. Toggle */
			rdp_send_scancode(ev_time, RDP_KEYPRESS, SCANCODE_CHAR_NUMLOCK);
			rdp_send_scancode(ev_time, RDP_KEYRELEASE, SCANCODE_CHAR_NUMLOCK);
		}
	}

}


static void
update_modifier_state(uint16 modifiers, BOOL pressed)
{

	DEBUG_KBD(("Before updating modifier_state:0x%x, pressed=0x%x\n",
		   remote_modifier_state, pressed));
	switch (modifiers)
	{
		case SCANCODE_CHAR_LSHIFT:
			MASK_CHANGE_BIT(remote_modifier_state, MapLeftShiftMask, pressed);
			break;
		case SCANCODE_CHAR_RSHIFT:
			MASK_CHANGE_BIT(remote_modifier_state, MapRightShiftMask, pressed);
			break;
		case SCANCODE_CHAR_LCTRL:
			MASK_CHANGE_BIT(remote_modifier_state, MapLeftCtrlMask, pressed);
			break;
		case SCANCODE_CHAR_RCTRL:
			MASK_CHANGE_BIT(remote_modifier_state, MapRightCtrlMask, pressed);
			break;
		case SCANCODE_CHAR_LALT:
			MASK_CHANGE_BIT(remote_modifier_state, MapLeftAltMask, pressed);
			break;
		case SCANCODE_CHAR_RALT:
			MASK_CHANGE_BIT(remote_modifier_state, MapRightAltMask, pressed);
			break;
		case SCANCODE_CHAR_LWIN:
			MASK_CHANGE_BIT(remote_modifier_state, MapLeftWinMask, pressed);
			break;
		case SCANCODE_CHAR_RWIN:
			MASK_CHANGE_BIT(remote_modifier_state, MapRightWinMask, pressed);
			break;
		case SCANCODE_CHAR_NUMLOCK:
			/* KeyReleases for NumLocks are sent immediately. Toggle the
			   modifier state only on Keypress */
			if (pressed)
			{
				BOOL newNumLockState;
				newNumLockState =
					(MASK_HAS_BITS
					 (remote_modifier_state, MapNumLockMask) == False);
				MASK_CHANGE_BIT(remote_modifier_state,
						MapNumLockMask, newNumLockState);
			}
			break;
	}
	DEBUG_KBD(("After updating modifier_state:0x%x\n", remote_modifier_state));

}

/* Send keyboard input */
void
rdp_send_scancode(uint32 time, uint16 flags, uint16 scancode)
{
	update_modifier_state(scancode, !(flags & RDP_KEYRELEASE));

	if (scancode & SCANCODE_EXTENDED)
	{
		DEBUG_KBD(("Sending extended scancode=0x%x, flags=0x%x\n",
			   scancode & ~SCANCODE_EXTENDED, flags));
		rdp_send_input(time, RDP_INPUT_SCANCODE, flags | KBD_FLAG_EXT,
			       scancode & ~SCANCODE_EXTENDED, 0);
	}
	else
	{
		DEBUG_KBD(("Sending scancode=0x%x, flags=0x%x\n", scancode, flags));
		rdp_send_input(time, RDP_INPUT_SCANCODE, flags, scancode, 0);
	}
}
