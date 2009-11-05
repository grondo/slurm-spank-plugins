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


#include <dlfcn.h>
#include "slurm.h"
#include "log.h"
/*
 *  Handle for libslurm.so
 *
 *  We open libslurm.so via dlopen () in order to pass the
 *   flag RTDL_GLOBAL so that subsequently loaded modules have
 *   access to libslurm symbols. This is pretty much only needed
 *   for dynamically loaded modules that would otherwise be
 *   linked against libslurm.
 *
 */
static void * slurm_h = NULL;


static int dyn_slurm_open () 
{
    if (slurm_h)
        return (0);
    if (!(slurm_h = dlopen("libslurm.so", RTLD_NOW|RTLD_GLOBAL))) {
        log_err ("Unable to dlopen libslurm: %s\n", dlerror ());
        return (-1);
    }
    return (0);
}

/*
 *  Wrapper for SLURM API function slurm_load_jobs ()
 */
int dyn_slurm_load_jobs (job_info_msg_t **msgp)
{
    static int (*load_jobs) (time_t, job_info_msg_t **) = NULL;

    dyn_slurm_open ();

    if (!load_jobs && !(load_jobs = dlsym (slurm_h, "slurm_load_jobs"))) {
        log_err ("Unable to resolve slurm_load_jobs\n");
        return -1;
    }

    return load_jobs ((time_t) NULL, msgp);
}

/*
 *  Wrapper for SLURM API function slurm_load_job ()
 *   (Implemented via slurm_load_jobs() if no symbol exists)
 */
int dyn_slurm_load_job (job_info_msg_t **msgp, uint32_t jobid)
{
    static int (*load_job) (job_info_msg_t **msgp, uint32_t jobid);

    dyn_slurm_open ();

    if (!load_job && !(load_job = dlsym (slurm_h, "slurm_load_job"))) {
        /*
         *  Fall back to slurm_load_jobs ()
         */
        return (dyn_slurm_load_jobs (msgp));
    }

    return load_job (msgp, jobid);
}


/*
 *  Wrapper for SLURM API function slurm_strerror ()
 */
char * dyn_slurm_strerror (int errnum)
{
    static char * (*f) (int) = NULL;

    dyn_slurm_open ();

    if (!f && !(f = dlsym (slurm_h, "slurm_strerror"))) {
        log_err ("Unable to resolve slurm_strerror\n");
        return "unknown error";
    }

    return f (errnum);
}


/*
 *  Wrapper for slurm_free_job_info_msg ()
 */
void dyn_slurm_free_job_info_msg (job_info_msg_t *msg)
{
    static void (*free_msg) (job_info_msg_t *) = NULL;

    dyn_slurm_open ();

    if (!free_msg && !(free_msg = dlsym (slurm_h, "slurm_free_job_info_msg"))) {
        log_err ("Unable to resolve slurm_free_job...\n");
        return;
    }

    free_msg (msg);

    return;
}

void dyn_slurm_close ()
{
    if (slurm_h) dlclose (slurm_h);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
