/* validate.h
 * Header file for validate.c
 *
 * Copyright (C) 1994,1996,2001 Olly Betts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef VALIDATE_H
#define VALIDATE_H

#include "debug.h"
#include "cavern.h"

bool validate(void);
void dump_node(node *stn);
void dump_network(void);

#if (VALIDATE==0)
# define validate() NOP
# define dump_node(S) NOP
#endif

#if (DUMP_NETWORK==0)
# define dump_network() NOP
#endif

#endif
