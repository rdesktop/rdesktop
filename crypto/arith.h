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

#ifndef	_arith_h_
#define	_arith_h_

#ifndef	_conf_h_
#include	"conf.h"
#endif

extern NUMBER a_one,a_two;

/*
 * Prototypes
 */

void	a_add		P(( NUMBER*, NUMBER*, NUMBER* ));
void	a_assign	P(( NUMBER*, NUMBER* ));
int	a_cmp		P(( NUMBER*, NUMBER* ));
void	a_div		P(( NUMBER*, NUMBER*, NUMBER*, NUMBER* ));
void	a_div2		P(( NUMBER* ));
void	a_ggt		P(( NUMBER*, NUMBER*, NUMBER* ));
void	a_imult		P(( NUMBER*, INT, NUMBER* ));
void	a_mult		P(( NUMBER*, NUMBER*, NUMBER* ));
void	a_sub		P(( NUMBER*, NUMBER*, NUMBER* ));
void	m_init		P(( NUMBER*, NUMBER* ));
void	m_add		P(( NUMBER*, NUMBER*, NUMBER* ));
void	m_mult		P(( NUMBER*, NUMBER*, NUMBER* ));
void	m_exp		P(( NUMBER*, NUMBER*, NUMBER* ));
int	n_bits		P(( NUMBER*, int));
void	n_div		P(( NUMBER*, NUMBER*, NUMBER*, NUMBER* ));
int	n_cmp		P(( INT*, INT*, int ));
int	n_mult		P(( INT*, INT, INT*, int ));
int	n_sub		P(( INT*, INT*, INT*, int, int ));
int	n_bitlen	P(( NUMBER* ));

#endif
