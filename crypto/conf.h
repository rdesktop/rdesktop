/*******************************************************************************
*									       *
*	Copyright (c) Martin Nicolay,  22. Nov. 1988			       *
*									       *
*	Wenn diese (oder sinngemaess uebersetzte) Copyright-Angabe enthalten   *
*	bleibt, darf diese Source fuer jeden nichtkomerziellen Zweck weiter    *
*	verwendet werden.						       *
*									       *
*	martin@trillian.megalon.de					       *
*									       *
*******************************************************************************/

#ifndef	_conf_h_
#define	_conf_h_

typedef	unsigned short INT;		/* muss MAXINT fassen		*/
typedef	unsigned long LONG;		/* muss (MAXINT+1)^2 -1 fassen	*/

#if	defined( M_XENIX )
#define	P(x)	x			/* Funktions Prototypen an	*/
#else
#define	P(x)	()			/* Funktions Prototypen aus	*/
#endif

/*
 *	(MAXINT+1)-adic Zahlen
 */

/*
 *	MAXINT		Maximale Zahl pro Elemenmt (muss int sein)
 *	MAXBIT		Maximales Bit von MAXINT
 *	LOWBITS		Anzahl der consekutiven low Bits von MAXINT
 *	HIGHBIT		Hoechsten Bit von MAXINT
 *	TOINT		muss (INT)( (x) % MAXINT) ergeben
 *	MAXLEN		Laenge der INT Array in jeder NUMBER
 */

#define MAXINT		0xFFFF

#if MAXINT == 99
#define	MAXBIT		7
#define	LOWBITS 	2
#endif
#if MAXINT == 9
#define	MAXBIT		4
#define	LOWBITS 	1
#endif
#if MAXINT == 1
#define MAXBIT		1
#endif
#if MAXINT == 0xFF
#define MAXBIT		8
#define	TOINT(x)	((INT)(x))		/* ACHTUNG !!!!! */
#endif
#if MAXINT == 0xFFFF
#define MAXBIT		16
#define	TOINT(x)	((INT)(x))		/* ACHTUNG !!!!! */
#endif

#ifndef	MAXBIT
#include	"<< ERROR: MAXBIT must be defined >>"
#endif
#ifndef	LOWBITS
#if MAXINT == (1 << MAXBIT) - 1
#define	LOWBITS		MAXBIT
#else
#include	"<< ERROR: LOWBITS must be defined >>"
#endif
#endif

#define	MAXLEN		(300*8/(MAXBIT + 1))
#define	STRLEN		(MAXLEN*MAXBIT/4)
#define	HIGHBIT		(1 << (MAXBIT-1) )

#if LOWBITS == MAXBIT
#define	DIVMAX1(x)	((x) >> MAXBIT)
#define	MODMAX1(x)	((x) & MAXINT)
#define	MULMAX1(x)	((x) << MAXBIT)
#else
#define	DIVMAX1(x)	((x) / (MAXINT+1))
#define	MODMAX1(x)	((x) % (MAXINT+1))
#define	MULMAX1(x)	((x) * (unsigned)(MAXINT+1))
#endif

#ifndef	TOINT
#define	TOINT(x)	((INT)MODMAX1(x))
#endif

typedef struct {
	int	n_len;			/* Hoechster benutzter Index	*/
	INT	n_part[MAXLEN];
} NUMBER;

#define	NUM0P	((NUMBER *)0)		/* Abkuerzung			*/

#endif
