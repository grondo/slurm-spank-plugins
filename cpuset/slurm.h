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


#ifndef _HAVE_DYN_SLURM_H
#define _HAVE_DYN_SLURM_H

#include <slurm/slurm.h>

int dyn_slurm_load_jobs (job_info_msg_t **msgp);
int dyn_slurm_load_job (job_info_msg_t **msgp, uint32_t jobid);
char * dyn_slurm_strerror (int errnum);
void dyn_slurm_free_job_info_msg (job_info_msg_t *msg);
void dyn_slurm_close ();

#endif
