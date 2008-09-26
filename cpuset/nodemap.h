/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 * 
 *  This file is part of chaos-spankings, a set of spank plugins for SLURM.
 * 
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/


#ifndef HAVE_NODEMAP_H
#define HAVE_NODEMAP_H

#include "conf.h"

/*
 *  Create a nodemap with optional used CPUs bitmask
 *   if used == NULL, then the nodemap will be initialized
 *   with the actual utilized CPUs.
 */
struct nodemap * nodemap_create (cpuset_conf_t cf, struct bitmask *used);
int nodemap_policy_update (struct nodemap *map, cpuset_conf_t cf);

void nodemap_destroy (struct nodemap *);

void print_nodemap (const struct nodemap *);

/*
 *  Allocate ncpus from nodemap 
 */
struct bitmask * nodemap_allocate (struct nodemap *map, int ncpus);

const struct bitmask * nodemap_used (struct nodemap *map);


#endif /* !HAVE_NODEMAP_H */
