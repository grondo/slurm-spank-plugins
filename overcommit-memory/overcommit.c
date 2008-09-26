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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "overcommit.h"
#include "fd.h"

static const char shared_filename [] = "/tmp/spank-overcommit-memory";
static const char overcommit_file [] = "/proc/sys/vm/overcommit_memory";
static const char overcommit_ratio_file [] = "/proc/sys/vm/overcommit_ratio";

struct overcommit_job_info {
    int jobid;
    int stepid;
    int  used;
};

struct overcommit_shared_info {
    sem_t sem;
    int initialized;
    int overcommit_value;
    int previous_overcommit_ratio;
    int nusers;
    struct overcommit_job_info users [64];
};

struct overcommit_shared_context {
    int fd;
    int jobid;
    int stepid;
    struct overcommit_shared_info *shared;
};

static int 
unregister_job (overcommit_shared_ctx_t ctx)
{
    int i;
    int maxn = sizeof (ctx->shared->users) / sizeof (int);

    for (i = 0; i < maxn; i++) {
        struct overcommit_job_info *j = &ctx->shared->users[i];

        if ((j->jobid == ctx->jobid)
           && ((ctx->stepid < 0) || (j->stepid == ctx->stepid))) {
            memset (j, 0, sizeof (*j));
            ctx->shared->nusers--;
            return (0);
        }
    }

    return (-1);
}

static int register_job (overcommit_shared_ctx_t ctx)
{
    int i;
    int maxn = sizeof (ctx->shared->users) / sizeof (int);

    for (i = 0; i < maxn; i++) {
        struct overcommit_job_info *j = &ctx->shared->users[i];
        if (!j->used) {
            j->used = 1;
            j->jobid = ctx->jobid;
            j->stepid = ctx->stepid;
            ctx->shared->nusers++;
            return (0);
        }
    }

    return (-1);
}

static int overcommit_shared_file_initialized (overcommit_shared_ctx_t ctx)
{
    struct stat st;

    if (fstat (ctx->fd, &st) < 0) {
        fprintf (stderr, "fstat (%s): %s\n", shared_filename, strerror (errno));
        return (-1);
    }

    if (st.st_uid != geteuid ()) {
        fprintf (stderr, "Bad owner on %s: uid=%d\n", 
                shared_filename, st.st_uid);
        return (-1);
    }

    if (st.st_size == sizeof (*ctx->shared))
        return (1);

    return (0);
}

static int overcommit_shared_info_init (overcommit_shared_ctx_t ctx)
{
    int len = sizeof (*ctx->shared);
    int initialized;

    if (ctx->fd < 0) {
        fprintf (stderr, "ctx->fd < 0!\n");
        return (-1);
    }
    if (fd_get_write_lock (ctx->fd) < 0)
        fprintf (stderr, "Failed to get write lock: %s\n", strerror (errno));

    if (fd_set_close_on_exec (ctx->fd))
        fprintf (stderr, "fd_set_close_on_exec(): %s\n", strerror (errno));

    if ((initialized = overcommit_shared_file_initialized (ctx)) < 0)
        return (-1);

    if (!initialized) 
        ftruncate (ctx->fd, len);

    ctx->shared = mmap (0, len, PROT_READ|PROT_WRITE, MAP_SHARED, ctx->fd, 0);

    if (ctx->shared == MAP_FAILED) {
        fprintf (stderr, "mmap (%s): %s\n", shared_filename, strerror (errno));
        return (-1);
    }

    if (!initialized) {
        memset (ctx->shared, 0, len);

        if (sem_init (&ctx->shared->sem, 1, 1) < 0) {
            fprintf (stderr, "sem_init: %s\n", strerror (errno));
            return (-1);
        }
    }

    if (fd_release_lock (ctx->fd) < 0)
        fprintf (stderr, "Failed to release file lock: %s\n", strerror (errno));

    return (0);
}

overcommit_shared_ctx_t overcommit_shared_ctx_attach ()
{
    overcommit_shared_ctx_t ctx = malloc (sizeof (*ctx));

    memset (ctx, 0, sizeof (*ctx));
    ctx->jobid = ctx->stepid = -1;

    if ((ctx->fd = open (shared_filename, O_RDWR)) < 0) 
        return (NULL);

    if (overcommit_shared_info_init (ctx) < 0) {
        overcommit_shared_ctx_destroy (ctx);
        return (NULL);
    }

    sem_wait (&ctx->shared->sem);

    return (ctx);
}

overcommit_shared_ctx_t 
overcommit_shared_ctx_create (int jobid, int stepid)
{
    int flags = O_RDWR | O_CREAT | O_EXCL;

    overcommit_shared_ctx_t ctx = malloc (sizeof (*ctx));

    if (!ctx)
        return (NULL);

    memset (ctx, 0, sizeof (*ctx));
    ctx->jobid = jobid;
    ctx->stepid = stepid;

    if ((ctx->fd = open (shared_filename, flags, 0600)) < 0) {
        if ((errno != EEXIST) 
            || ((ctx->fd = open (shared_filename, O_RDWR)) < 0)) {
            fprintf (stderr, "Failed to open overcommit shared info: %s",
                    strerror (errno));
            overcommit_shared_ctx_destroy (ctx);
            return (NULL);
        }
    }


    if (overcommit_shared_info_init (ctx) < 0) {
        overcommit_shared_ctx_destroy (ctx);
        return (0);
    }

    sem_wait (&ctx->shared->sem);

    return (ctx);
}


int overcommit_shared_cleanup (int jobid, int stepid)
{
    int rc = 0;
    overcommit_shared_ctx_t ctx;

    if ((ctx = overcommit_shared_ctx_create (jobid, stepid))) {
        rc = unregister_job (ctx);
        overcommit_shared_ctx_destroy (ctx);
    } else if (overcommit_memory_get_current_state () != 0) {
        overcommit_memory_set_current_state (0);
    }
    return (rc);
}

int overcommit_force_cleanup ()
{
    if ((unlink (shared_filename) < 0) && (errno != ENOENT))
        fprintf (stderr, "Failed to remove %s: %s\n", shared_filename, 
                strerror (errno));
    if (overcommit_memory_get_current_state () != 0) {
        return (overcommit_memory_set_current_state (0));
        overcommit_ratio_set (50); /* XXX: Need a way to set default!! */
    }
    return (0);
}

void overcommit_shared_ctx_destroy (overcommit_shared_ctx_t ctx)
{
    if (ctx->shared->nusers == 0) {
        unlink (shared_filename);
        if (overcommit_memory_get_current_state () != 0)
           overcommit_memory_set_current_state (0);
        overcommit_ratio_set (ctx->shared->previous_overcommit_ratio);
    }
    sem_post (&ctx->shared->sem);
    munmap (ctx->shared, sizeof (*ctx->shared));
    close (ctx->fd);
    free (ctx);
}

void overcommit_shared_ctx_unregister (overcommit_shared_ctx_t ctx)
{
    sem_wait (&ctx->shared->sem);
    unregister_job (ctx);
    overcommit_shared_ctx_destroy (ctx);
}

int overcommit_shared_list_users ()
{
    overcommit_shared_ctx_t ctx;
    int i;
    int maxn = sizeof (ctx->shared->users) / sizeof (int);

    if (!(ctx = overcommit_shared_ctx_attach ()) || ctx->shared->nusers == 0) {
        fprintf (stdout, "No users currently using overcommit-memory\n");
        return (0);
    }

    fprintf (stdout, "%d users of overcommit-memory on this node:\n", 
            ctx->shared->nusers);

    for (i = 0; i < maxn; i++) {
        struct overcommit_job_info *j = &ctx->shared->users[i];
        if (j->used)
            fprintf (stdout, "%d.%d\n", j->jobid, j->stepid);
    }
    fprintf (stdout, "\n");
    fprintf (stdout, "Current setting = %d\n", ctx->shared->overcommit_value);
    fprintf (stdout, "Current ratio =   %d\n", overcommit_ratio_get ());
    fprintf (stdout, "Previous ratio =  %d\n", 
            ctx->shared->previous_overcommit_ratio);


    overcommit_shared_ctx_destroy (ctx);
    return (0);
}

int overcommit_in_use (overcommit_shared_ctx_t ctx, int value)
{
    int rc = 0;
    if ((ctx->shared->nusers > 0) && (ctx->shared->overcommit_value != value))
        rc = 1;
    else {
        if (!ctx->shared->nusers) {
            ctx->shared->overcommit_value = value;
            ctx->shared->previous_overcommit_ratio = overcommit_ratio_get ();
        }
        register_job (ctx);
    }
    sem_post (&ctx->shared->sem);

    return (rc);
}

int overcommit_memory_get_current_state ()
{
    int val = -1;
    FILE *fp;

    if (!(fp = fopen (overcommit_file, "r"))) 
        return (-1);

    fscanf (fp, "%d", &val);

    fclose (fp);

    return (val);
}

int overcommit_memory_set_current_state (int val)
{
    FILE *fp;

    if (val > 2 || val < 0)
        return (-1);

    if (!(fp = fopen (overcommit_file, "w"))) {
        fprintf (stderr, "open (%s): %s\n", overcommit_file, strerror (errno));
        return (-1);
    }

    fprintf (fp, "%d\n", val);

    fclose (fp);

    return (0);
}

int overcommit_ratio_set (int val)
{
    FILE *fp;

    if (!(fp = fopen (overcommit_ratio_file, "w")))
        return (-1);

    fprintf (fp, "%d\n", val);

    fclose (fp);

    return (0);
}

int overcommit_ratio_get ()
{
    int val = -1;
    FILE *fp;

    if (!(fp = fopen (overcommit_ratio_file, "r"))) 
        return (-1);

    fscanf (fp, "%d", &val);

    fclose (fp);

    return (val);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
