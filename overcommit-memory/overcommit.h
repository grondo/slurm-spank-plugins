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

#ifndef _HAVE_OVERCOMMIT_H
#define _HAVE_OVERCOMMIT_H

typedef struct overcommit_shared_context * overcommit_shared_ctx_t;

overcommit_shared_ctx_t overcommit_shared_ctx_create (int jobid, int stepid);

void overcommit_shared_ctx_destroy (overcommit_shared_ctx_t ctx);
void overcommit_shared_ctx_unregister (overcommit_shared_ctx_t ctx);

int overcommit_in_use (overcommit_shared_ctx_t ctx, int value);
int overcommit_shared_list_users ();

int overcommit_shared_cleanup (int jobid, int stepid);
int overcommit_force_cleanup ();

int overcommit_memory_get_current_state ();
int overcommit_memory_set_current_state (int value);

int overcommit_ratio_get ();
int overcommit_ratio_set (int value);

#endif /* !_HAVE_OVERCOMMIT_H */
