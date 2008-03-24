#include <windows.h>
#include <stdio.h>
#include <stdlib.h>



int
main(int argc, char *argv[])
{
	HWND notepad, wordpad;

	notepad = FindWindow("Notepad", NULL);
	wordpad = FindWindow("WordPadClass", NULL);

	SetWindowPos(notepad, wordpad, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

	return 0;
}
