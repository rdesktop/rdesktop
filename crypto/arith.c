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

#include	"arith.h"

/*
 *	!!!!!!!!!!!!!!!!!!!!!!!!!!!! ACHTUNG !!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *	Es findet keinerlei Ueberpruefung auf Bereichsueberschreitung
 *	statt. Alle Werte muessen natuerliche Zahlen aus dem Bereich
 *		0 ... (MAXINT+1)^MAXLEN-1 sein.
 *	
 *	
 *	Bei keiner Funktion oder Hilsfunktion werden Annahmen getroffen,
 *	ueber die Verschiedenheit von Eingabe- & Ausgabe-Werten.
 *
 *
 *		Funktionen:
 *
 *	a_add( s1, s2, d )
 *		NUMBER *s1,*s2,*d;
 *			*d = *s1 + *s2;
 *
 *	a_assign( *d, *s )
 *		NUMBER *d,*s;
 *			*d = *s;
 *
 * int	a_cmp( c1, c2 )
 *		NUMBER *c1,*c2;
 *			 1 :	falls *c1 >  *c2
 *			 0 :	falls *c1 == *c2
 *			-1 :	falls *c1 <  *c2
 *
 *	a_div( d1, d2, q, r )
 *		NUMBER *d1,*d2,*q,*r;
 *			*q = *d1 / *d2 Rest *r;
 *
 *	a_div2( n )
 *		NUMBER *n;
 *			*n /= 2;
 *
 *	a_ggt( a, b, f )
 *		NUMBER *a,*b,*f;
 *			*f = ( *a, *b );
 *
 *	a_imult( n, m, d )
 *		NUMBER *n;
 *		INT m;
 *		NUMBER *d;
 *			*d = *n * m
 *
 *	a_mult( m1, m2, d )
 *		NUMBER *m1,*m2,*d;
 *			*d = *m1 * *m2;
 *
 *	a_sub( s1, s2, d )
 *		NUMBER *s1,*s2,*d;
 *			*d = *s1 - *s2;
 *
 *		Modulare Funktionen
 *	m_init( n, o )
 *		NUMBER *n,*o;
 *			Initialsierung der Modularen Funktionen
 *			o != 0 : *o = alter Wert
 *
 *	m_add( s1, s2, d )
 *		NUMBER *s1, *s2, *d;
 *			*d = *s1 + *s2;
 *
 *	m_mult( m1, m2, d )
 *		NUMBER *m1,*m2,*d;
 *
 *	m_exp( x, n, z )
 *		NUMBER *x,*n,*z;
 *			*z = *x exp *n;
 *
 *
 *		Hilfs-Funktionen:
 *
 * int	n_bits( n, b )
 *		NUMBER *n;
 *		int b;
 *			return( unterste b Bits der Dualdarstellung von n)
 *
 *	n_div( d1, z2, q, r )
 *		NUMBER *d1,z2[MAXBIT],*q,*r;
 *			*q = *d1 / z2[0] Rest *r;
 *			z2[i] = z2[0] * 2^i,  i=0..MAXBIT-1
 *
 * int	n_cmp( i1, i2, l )
 *		INT i1[l], i2[l];
 *			 1 :	falls i1 >  i2
 *			 0 :	falls i1 == i2
 *			-1 :	falls i1 <  i2
 *
 * int	n_mult( n, m, d, l)
 *		INT n[l], m, d[];
 *			d = m * n;
 *			return( sizeof(d) ); d.h. 'l' oder 'l+1'
 *
 * int	n_sub( p1, p2, p3, l, lo )
 *		INT p1[l], p2[lo], p3[];
 *			p3 = p1 - p2;
 *			return( sizeof(p3) ); d.h. '<= min(l,lo)'
 *
 * int	n_bitlen( n )
 * 		NUMBER *n;
 *			return( sizeof(n) in bits )
 *
 */


/*
 * Konstante 1, 2
 */
NUMBER a_one = {
	1,
	{ (INT)1, },
};

NUMBER a_two = {
#if MAXINT == 1
	2,
	{ 0, (INT)1, },
#else
	1,
	{ (INT)2, },
#endif
};


/*
 * Vergleiche zwei INT arrays der Laenge l
 */
int n_cmp( i1, i2, l )
INT *i1,*i2;
{
	i1 += (l-1);			/* Pointer ans Ende		*/ 
	i2 += (l-1);
	 
	for (;l--;)
		if ( *i1-- != *i2-- )
			return( i1[1] > i2[1] ? 1 : -1 );
	
	return(0);
}

/*
 * Vergleiche zwei NUMBER
 */
int a_cmp( c1, c2 )
NUMBER *c1,*c2;
{
	int l;
					/* bei verschiedener Laenge klar*/
	if ( (l=c1->n_len) != c2->n_len)
		return( l - c2->n_len);
	
					/* vergleiche als arrays	*/
	return( n_cmp( c1->n_part, c2->n_part, l) );
}

/*
 * Zuweisung einer NUMBER (d = s)
 */
void a_assign( d, s )
NUMBER *d,*s;
{
	int l;
	
	if (s == d)			/* nichts zu kopieren		*/
		return;
	
	if ((l=s->n_len))
		memcpy( d->n_part, s->n_part, sizeof(INT)*l);
	
	d->n_len = l;
}

/*
 * Addiere zwei NUMBER (d = s1 + s2)
 */
void a_add( s1, s2, d )
NUMBER *s1,*s2,*d;
{
	int l,lo,ld,same;
	register LONG sum;
	register INT *p1,*p2,*p3;
	register INT b;
	
				/* setze s1 auch die groessere Zahl	*/
	l = s1->n_len;
	if ( (l=s1->n_len) < s2->n_len ) {
		NUMBER *tmp = s1;
		
		s1 = s2;
		s2 = tmp;
		
		l = s1->n_len;
	}

	ld = l;
	lo = s2->n_len;
	p1 = s1->n_part;
	p2 = s2->n_part;
	p3 = d->n_part;
	same = (s1 == d);
	sum = 0;
	
	while (l --) {
		if (lo) {		/* es ist noch was von s2 da	*/
			lo--;
			b = *p2++;
		}
		else
			b = 0;		/* ansonten 0 nehmen		*/
		
		sum += (LONG)*p1++ + (LONG)b;
		*p3++ = TOINT(sum);

		if (sum > (LONG)MAXINT) {	/* carry merken		*/
			sum = 1;
		}
		else
			sum = 0;

		if (!lo && same && !sum)	/* nichts mehr zu tuen	*/
			break;
	}
	
	if (sum) {		/* letztes carry beruecksichtigen	*/
		ld++;
		*p3 = sum;
	}
	
	d->n_len = ld;			/* Laenge setzen		*/
}

/*
 * Subtrahiere zwei INT arrays. return( Laenge Ergebniss )
 * l == Laenge p1
 * lo== Laenge p3
 */
int n_sub( p1, p2, p3, l, lo )
INT *p1,*p2,*p3;
int l,lo;
{
	int ld,lc,same;
	int over = 0;
	register LONG dif;
	LONG a,b;
	
	same = (p1 == p3);			/* frueher Abbruch moeglich */
	
	for (lc=1, ld=0; l--; lc++) {
		a = (LONG)*p1++;
		if (lo) {			/* ist noch was von p2 da ? */
			lo--;
			b = (LONG)*p2++;
		}
		else
			b=0;			/* ansonten 0 nehmen	*/

		if (over)			/* frueherer Overflow	*/
			b++;
		if ( b > a ) {			/* jetzt Overflow ?	*/
			over = 1;
			dif = (MAXINT +1) + a;
		}
		else {
			over = 0;
			dif = a;
		}
		dif -= b;
		*p3++ = (INT)dif;
		
		if (dif)			/* Teil != 0 : Laenge neu */
			ld = lc;
		if (!lo && same && !over) {	/* nichts mehr zu tuen	*/
			if (l > 0)		/* Laenge korrigieren	*/
				ld = lc + l;
			break;
		}
	}

	return( ld );
}

/*
 * Subtrahiere zwei NUMBER (d= s1 - s2)
 */
void a_sub( s1, s2, d )
NUMBER *s1,*s2,*d;
{
	d->n_len = n_sub( s1->n_part, s2->n_part, d->n_part
			 ,s1->n_len, s2->n_len );
}

/*
 * Mulitipliziere INT array der Laenge l mit einer INT (d = n * m)
 * return neue Laenge
 */
int n_mult( n, m, d, l)
register INT *n;
register INT m;
INT *d;
{
	int i;
	register LONG mul;
	
	for (i=l,mul=0; i; i--) {
		mul += (LONG)m * (LONG)*n++;
		*d++ = TOINT(mul);
		mul  = DIVMAX1( mul );
	}
	
	if (mul) {		/* carry  ? */
		l++;
		*d = mul;
	}
	
	return( l );
}

/*
 * Mulitipliziere eine NUMBER mit einer INT (d = n * m)
 */
void a_imult( n, m, d )
NUMBER *n;
INT m;
NUMBER *d;
{
	if (m == 0)
		d->n_len=0;
	else if (m == 1)
		a_assign( d, n );
	else
		d->n_len = n_mult( n->n_part, m, d->n_part, n->n_len );
}
  
/*
 * Multipliziere zwei NUMBER (d = m1 * m2) 
 */
void a_mult( m1, m2, d )
NUMBER *m1,*m2,*d;
{
	static INT id[ MAXLEN ];		/* Zwischenspeicher	*/
	register INT *vp;			/* Pointer darin	*/
	register LONG sum;			/* Summe fuer jede Stelle */
	register LONG tp1;			/* Zwischenspeicher fuer m1 */
	register INT *p2;
	INT *p1;
	int l1,l2,ld,lc,l,i,j;
	
	l1 = m1->n_len;
	l2 = m2->n_len;
	l = l1 + l2;
	if (l >= MAXLEN)
		abort();

	for (i=l, vp=id; i--;)
		*vp++ = 0;
	
			/* ohne Uebertrag in Zwischenspeicher multiplizieren */
	for ( p1 = m1->n_part, i=0; i < l1 ; i++, p1++ ) {

		tp1 = (LONG)*p1;		
		vp = &id[i];
		sum = 0;
		for ( p2 = m2->n_part, j = l2; j--;) {
			sum += (LONG)*vp + (tp1 * (LONG)*p2++);
			*vp++ = TOINT( sum );
			sum = DIVMAX1(sum);
		}
		*vp++ += (INT)sum;
	}

			/* jetzt alle Uebertraege beruecksichtigen	*/
	ld = 0;
	for (lc=0, vp=id, p1=d->n_part; lc++ < l;) {
		if (( *p1++ = *vp++ ))
			ld = lc;
	}
	
	d->n_len = ld;
}


/*
 * Dividiere Zwei NUMBER mit Rest (q= d1 / z2[0] Rest r)
 * z2[i] = z2[0] * 2^i,  i=0..MAXBIT-1
 * r = 0 : kein Rest
 * q = 0 : kein Quotient
 */
void n_div( d1, z2, q, r )
NUMBER *d1,*z2,*q,*r;
{
	static	NUMBER dummy_rest;  /* Dummy Variable, falls r = 0 */
	static	NUMBER dummy_quot;  /* Dummy Variable, falla q = 0 */
	INT *i1,*i1e,*i3;
	int l2,ld,l,lq;
#if MAXINT != 1
	INT z;
	int pw,l2t;
#endif

	if (!z2->n_len)
		abort();
		
	if (!r)
		r = &dummy_rest;
	if (!q)
		q = &dummy_quot;
	
	a_assign( r, d1 );	/* Kopie von d1 in den Rest		*/
	
	l2= z2->n_len;		/* Laenge von z2[0]			*/
	l = r->n_len - l2;	/* Laenge des noch ''rechts'' liegenden
					Stuecks von d1			*/
	lq= l +1;		/* Laenge des Quotienten		*/
	i3= q->n_part + l;
	i1= r->n_part + l;
	ld = l2;		/* aktuelle Laenge des ''Vergleichsstuecks''
					von d1				*/
	i1e= i1 + (ld-1);
	
	for (; l >= 0; ld++, i1--, i1e--, l--, i3--) {
		*i3 = 0;

		if (ld == l2 && ! *i1e) {
			ld--;
			continue;
		}
		
		if ( ld > l2 || (ld == l2 && n_cmp( i1, z2->n_part, l2) >= 0 ) ) {
#if MAXINT != 1
				/* nach 2er-Potenzen zerlegen	*/
			for (pw=MAXBIT-1, z=(INT)HIGHBIT; pw >= 0; pw--, z /= 2) {
				if ( ld > (l2t= z2[pw].n_len)
					|| (ld == l2t
					    && n_cmp( i1, z2[pw].n_part, ld) >= 0) ) {
					ld = n_sub( i1, z2[pw].n_part, i1, ld, l2t );
					(*i3) += z;
				}
			}
#else
				/* bei MAXINT == 1 alles viel einfacher	*/
			ld = n_sub( i1, z2->n_part, i1, ld, l2 );
			(*i3) ++;
#endif
		}
	}
	
			/* Korrektur, falls l von Anfang an Negativ war */
	l ++;
	lq -= l;
	ld += l;
	
	if (lq>0 && !q->n_part[lq -1])	/* evtl. Laenge korrigieren	*/
		lq--;
	
	q->n_len = lq;
	r->n_len = ld -1;
}

/*
 * Dividiere Zwei NUMBER mit Rest (q= d1 / z2[0] Rest r)
 * z2[i] = z2[0] * 2^i,  i=0..MAXBIT-1
 * r = 0 : kein Rest
 * q = 0 : kein Quotient
 */
void a_div( d1, d2, q, r )
NUMBER *d1,*d2,*q,*r;
{
#if MAXINT != 1
	NUMBER z2[MAXBIT];
	INT z;
	int i;
	
	a_assign( &z2[0], d2 );
	for (i=1,z=2; i < MAXBIT; i++, z *= 2)
		a_imult( d2, z, &z2[i] );
	
	d2 = z2;
#endif

	n_div( d1, d2, q, r );
}

/*
 * Dividiere eine NUMBER durch 2
 */
void a_div2( n )
NUMBER *n;
{
#if MAXBIT == LOWBITS
	register INT *p;
	int i;

#if MAXINT != 1
	register INT h;
	register int c;

	c=0;
	i= n->n_len;
	p= &n->n_part[i-1];
	
	for (; i--;) {
		if (c) {
			c = (h= *p) & 1;
			h /= 2;
			h |= HIGHBIT;
		}
		else {
			c = (h= *p) & 1;
			h /= 2;
		}
		
		*p-- = h;
	}
	
	if ( (i= n->n_len) && n->n_part[i-1] == 0 )
		n->n_len = i-1;

#else  /* MAXBIT != 1 */
	p = n->n_part;
	i = n->n_len;
	
	if (i) {
		n->n_len = i-1;
		for (; --i ; p++)
			p[0] = p[1];
	}
#endif /* MAXBIT != 1 */
#else  /* MAXBIT == LOWBITS */
	a_div( n, &a_two, n, NUM0P );
#endif /* MAXBIT == LOWBITS */
}


/*
 *	MODULO-FUNKTIONEN
 */

static NUMBER mod_z2[ MAXBIT ];

/*
 * Init
 */
void m_init( n, o )
NUMBER *n,*o;
{
	INT z;
	int i;
	
	if (o)
		a_assign( o, &mod_z2[0] );
	
	if (! a_cmp( n, &mod_z2[0] ) )
		return;
	
	for (i=0,z=1; i < MAXBIT; i++, z *= 2)
		a_imult( n, z, &mod_z2[i] );
}

void m_add( s1, s2, d )
NUMBER *s1, *s2, *d;
{
	a_add( s1, s2, d );
	if (a_cmp( d, mod_z2 ) >= 0)
		a_sub( d, mod_z2, d );
}

void m_mult( m1, m2, d )
NUMBER *m1,*m2,*d;
{
	a_mult( m1, m2, d );
	n_div( d, mod_z2, NUM0P, d );
}

/*
 * Restklassen Exponent
 */
void m_exp( x, n, z )
NUMBER *x,*n,*z;
{
	NUMBER xt,nt;
	
	a_assign( &nt, n );
	a_assign( &xt, x );
	a_assign( z, &a_one );
	
	while (nt.n_len) {
		while ( ! (nt.n_part[0] & 1) ) {
			m_mult( &xt, &xt, &xt );
			a_div2( &nt );
		}
		m_mult( &xt, z, z );
		a_sub( &nt, &a_one, &nt );
	}
}

/*
 * GGT
 */
void a_ggt( a, b, f )
NUMBER *a,*b,*f;
{
	NUMBER t[2];
	int at,bt, tmp;
	
	a_assign( &t[0], a ); at= 0;
	a_assign( &t[1], b ); bt= 1;
	
	if ( a_cmp( &t[at], &t[bt] ) < 0 ) {
		tmp= at; at= bt; bt= tmp;
	}
				/* euklidischer Algorithmus		*/
	while ( t[bt].n_len ) {
		a_div( &t[at], &t[bt], NUM0P, &t[at] );
		tmp= at; at= bt; bt= tmp;
	}
	
	a_assign( f, &t[at] );
}
	
/*
 * die untersten b bits der Dualdarstellung von n
 * die bits muessen in ein int passen
 */
int n_bits(n,b)
NUMBER *n;
{
	INT *p;
	int l;
	unsigned r;
	int m = (1<<b) -1;

	if ( n->n_len == 0)
		return(0);
	
	if (LOWBITS >= b)
		return( n->n_part[0] & m );

#if LOWBITS != 0
	l = (b-1) / LOWBITS;
#else
	l = n->n_len -1;
#endif
	for (p= &n->n_part[l],r=0; l-- >= 0 && b > 0; b-= LOWBITS, p--) {
		r  = MULMAX1( r );
		r += (unsigned)*p;
	}

	return( r & m );
}

/*
 * Anzahl der bits von n bei Dualdarstellung
 */
int n_bitlen( n )
NUMBER *n;
{
	NUMBER b;
	int i;
	
	a_assign( &b, &a_one );
	
	for (i=0; a_cmp( &b, n ) <= 0; a_mult( &b, &a_two, &b ), i++)
		;
	
	return(i);
}
