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

/*############################################################################
 *  $Id$
 *############################################################################
 *
 *  SLURM spank plugin to detect tasks killed by OOM killer using CHAOS
 *   kernel /proc/oomkilled file. 
 *
 *  Requires SGI Job container-based process tracking.
 *  
 *############################################################################
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>


#include <job.h>
#include <slurm/spank.h>

SPANK_PLUGIN(oom-detect, 1)

typedef jid_t (*getjid_f) (pid_t pid);

static int                do_syslog = 0;

static void *             libjob =   NULL;
static getjid_f           getjid =   NULL;
static jid_t              jid =      (jid_t) -1;
static uint32_t           ntasks =   (uint32_t) -1;


int slurm_spank_init (spank_t sp, int ac, char *av[])
{
    if (!spank_remote (sp))
        return (0);

    if (ac > 0 && strcmp (av[0], "do_syslog") == 0)
        do_syslog = 1;

    if (!(libjob = dlopen ("libjob.so", RTLD_LAZY))) {
        slurm_error ("Failed to open libjob.so: %s", dlerror ());
        return (-1);
    }

    if (!(getjid = (getjid_f) dlsym (libjob, "job_getjid"))) {
        slurm_error ("Failed to resolve job_getjid in libjob: %s", 
                dlerror ());
        return (-1);
    }

    /*
     *  spank_init runs after slurm job container has been created, so
     *   now determine our jid. 
     */
    if ((jid = (*getjid) (getpid ())) == (jid_t) -1) 
        slurm_info ("Failed to get job container id");

    if (spank_get_item (sp, S_JOB_LOCAL_TASK_COUNT, &ntasks))  {
        slurm_error ("spank_get_item (S_JOB_LOCAL_TASK_COUNT) failed.");
        /* must be at least one task */
        ntasks = 1;
    }

    return (0);
}

#define OOMKILLED_FILENAME  "/proc/oomkilled"

struct oomkilled_data {
    uint64_t jobid;
    pid_t pid;
    long vmsize;
    long rss;
    char comm[16];
};

static int _parse_oomkilled (struct oomkilled_data *data, size_t size)
{
    char buf [4096];
    char *bufptr;
    char *line;
    ssize_t len;
    int count = 0;
    int fd = -1;
    int rv = -1;

    assert(data && size);

    if (access (OOMKILLED_FILENAME, R_OK) < 0) {
        goto cleanup;
    }

    if ((fd = open (OOMKILLED_FILENAME, O_RDONLY)) < 0) {
        goto cleanup;
    }

    memset(buf, '\0', sizeof (buf));
    if ((len = read (fd, buf, sizeof (buf))) < 0) {
        goto cleanup;
    }

    if (!len)
        return 0;


    line = strtok_r (buf, "\n", &bufptr);
    do {
        struct oomkilled_data *d;

        if (count >= size) {
            errno = ENOSPC;
            goto cleanup;
        }

        d = &data[count];
        memset (d, 0, sizeof (*d));
        if (sscanf (line, "%lu %d %ld %ld %15c", 
                    &d->jobid, &d->pid, &d->vmsize, &d->rss, d->comm) != 5) {
            goto cleanup;
        }
        count++;

    } while ((line = strtok_r (NULL, "\n", &bufptr))); 

    rv = count;
cleanup:
    close(fd);
    return rv;
}


int oomkilled_pids (jid_t jid, struct oomkilled_data *d, size_t len)
{
    struct oomkilled_data data [64];
    int count;
    int i;
    int index = 0;

    if ((count = _parse_oomkilled (data, 64)) < 0) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if ((jid_t) data[i].jobid == jid) {
            if (index >= len) {
                errno = ENOSPC;  
                return -1;
            }
            d[index++] = data[i];
        }
    }

    return (index);
}

static int pid_reported (pid_t pid)
{
    static pid_t pids[64];
    static int initialized = 0;
    int i = 0;

    if (!initialized) {
        memset (pids, 0, sizeof (pids));
        initialized = 1;
    }

    for (i = 0; i < 64; i++) {
        if (pids[i] == 0) {
            pids[i] = pid;
            return (0);
        }
        if (pids[i] == pid)
            return (1);
    }

    return (0);
}

static void print_oomkilled_error (struct oomkilled_data *d, int taskid)
{
    char buf [256];
    const size_t siz = sizeof (buf);
    int len = 0;

    memset (buf, 0, sizeof (buf));

    if (d->vmsize) {
        if ((len = snprintf (buf, siz, " VmSize: %ldM", d->vmsize/1024)) < 0)
            len = 0;
    }
    if (d->rss)
        len += snprintf (buf+len, siz - len, " RSS: %ldM", d->rss/1024);

    if ((len >= siz)) {
        buf [siz - 2] = '+';
        buf [siz - 1] = '\0';
    }

    if (taskid >= 0) {
        slurm_error ("task%d: [%s] terminated by OOM killer.", taskid, d->comm);
        if (d->vmsize || d->rss)
            slurm_error ("task%d:%s", taskid, buf);
    } else {
        slurm_error ("pid %ld: [%s] %s terminated by OOM killer.\n",
                d->pid, d->comm, "(task id unknown)");
        if (d->vmsize || d->rss)
            slurm_error ("pid %ld:%s", d->pid, buf);
    }
    return;
}

static void send_syslog_oom_msg (spank_t sp)
{
    uint32_t jobid;
    uint32_t stepid;
    uid_t uid;

    if ((spank_get_item (sp, S_JOB_ID, &jobid) != ESPANK_SUCCESS) ||
        (spank_get_item (sp, S_JOB_STEPID, &stepid) != ESPANK_SUCCESS) || 
        (spank_get_item (sp, S_JOB_UID, &uid) != ESPANK_SUCCESS)) {
        slurm_error ("Failed to get jobid, stepid, or uid for syslog msg.");
        return;
    }

    openlog ("slurmd", 0, LOG_USER);
    syslog (LOG_WARNING, "OOM detected: jobid=%u.%u uid=%u", jobid, stepid, uid);
    closelog ();
    slurm_verbose ("Sent OOM message via syslog for this job.");

}

int slurm_spank_task_exit (spank_t sp, int ac, char *av[])
{
    static int nexited = 0;
    struct oomkilled_data killed [16];
    int   n;
    int   i;

    if ((jid == (jid_t) -1) || (ntasks == (uint32_t) -1))
        return (0);

    ++nexited;

    /*
     *  As each task exits, report to user if any processes
     *   were terminated by OOM killer
     */
    if (!(n = oomkilled_pids (jid, killed, 16))) 
        return (0);

    for (i = 0; i < n; i++) {
        struct oomkilled_data *d = &killed[i];
        uint32_t taskid;

        if (pid_reported (d->pid))
            continue;

        spank_get_item (sp, S_JOB_PID_TO_GLOBAL_ID, d->pid, &taskid); 

        print_oomkilled_error (d, taskid);
    }

    if (nexited == ntasks) {
        if (do_syslog) 
            send_syslog_oom_msg (sp);
        /*
         *  If we got here, then we printed one or more OOM killed message 
         *   to user's  stderr. Delay a bit here to make it more likely 
         *   that the user gets the message.
         */
        sleep (2);
    }
    return (0);
}

int slurm_spank_exit (spank_t sp, int ac, char *av[])
{
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
