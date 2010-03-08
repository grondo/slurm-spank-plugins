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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#define __USE_GNU 1
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "fd.h"
#include "util.h"
#include "nodemap.h"
#include "create.h"
#include "slurm.h"
#include "log.h"

/*
 *  Path to base SLURM cpuset, which contains all other cpusets.
 */
static const char slurm_cpuset[] = "/dev/cpuset/slurm";

void print_bitmask (const char *fmt, const struct bitmask *b)
{
    char buf [16];
    bitmask_displaylist (buf, sizeof (buf), b);
    log_msg (fmt, buf);
}

static struct cpuset * get_cpuset (const char *path)
{
    struct cpuset *cpuset = NULL;

    if (!(cpuset = cpuset_alloc ()))
        return (NULL);

    if (cpuset_query (cpuset, path) < 0) {
        cpuset_free (cpuset);
        return (NULL);
    }

    return (cpuset);
}

int cpumask_size (void)
{
    struct cpuset *cp;
    static int totalcpus = -1;
    if (totalcpus < 0) {
        cp = get_cpuset ("/");
        totalcpus = cpuset_cpus_weight (cp);
        cpuset_free (cp);
    }
    return (totalcpus);
}

int memmask_size (void)
{
    struct cpuset *cp;
    static int totalmems = -1;
    if (totalmems < 0) {
        cp = get_cpuset ("/");
        totalmems = cpuset_mems_weight (cp);
        cpuset_free (cp);
    }
    return (totalmems);
}

void print_cpuset_info (const char *path, struct cpuset *cp)
{
    char cstr [16];
    char mstr [16];
    struct bitmask *cpus, *mems;
    int ncpus, nmems;

    ncpus = cpuset_cpus_weight (cp);
    nmems = cpuset_mems_weight (cp);

    cpus = bitmask_alloc (cpumask_size ());
    mems = bitmask_alloc (memmask_size ());
    
    cpuset_getcpus (cp, cpus);
    cpuset_getmems (cp, mems);

    bitmask_displaylist (cstr, sizeof (cstr), cpus);
    bitmask_displaylist (mstr, sizeof (mstr), mems);

    cpuset_verbose ("%s: %d cpu%s [%s], %d mem%s [%s]\n", 
            path,
            ncpus, (ncpus == 1 ? "" : "s"), cstr, 
            nmems, (nmems == 1 ? "" : "s"), mstr);

    bitmask_free (cpus);
    bitmask_free (mems);
}

void print_current_cpuset_info ()
{
    char path [4096];
    struct cpuset *cp = cpuset_alloc ();

    cpuset_getcpusetpath (0, path, sizeof (path));
    cpuset_query (cp, path);

    print_cpuset_info (path, cp);

    cpuset_free (cp);
}

static int current_cpuset_path (char *path, int len)
{
    if (len < 12) 
        return (-1);

    strncpy (path, "/dev/cpuset", len);

    if (!cpuset_getcpusetpath (0, path + 11, len - 11))
        return (-1);

    if (strcmp (path, "/dev/cpuset/") == 0) {
        /*
         *  If we are in the root cpuset, pretend we're in /slurm instead.
         */
        strncat (path, "slurm", len);
    }

    return (0);
}

const char * cpuset_path_to_name (const char *path)
{
    return (path + 11);
}

struct bitmask *used_cpus_bitmask_path (char *path, int clearall)
{
    char buf [4096];
    const char *current;
    struct bitmask *b, *used;
    DIR *dirp;
    struct dirent *dp;
    struct cpuset *cp;

    if (path == NULL) {
        path = buf;
        if (current_cpuset_path (buf, sizeof (buf)) < 0) {
            cpuset_error ("Unable to get current cpuset path: %m");
            return (NULL);
        }
        cpuset_debug ("used_cpus_bitmask_path (%s)\n", path);
    }

    if ((dirp = opendir (path)) == NULL) {
        cpuset_error ("Couldn't open %s: %m", path);
        return NULL;
    }

    if ((cp = cpuset_alloc ()) == NULL) {
        cpuset_error ("Couldn't alloc cpuset: %m");
        return (NULL);
    }

    current = cpuset_path_to_name (path);

    b = bitmask_alloc (cpumask_size ());
    used = bitmask_alloc (cpumask_size ());

    if (!clearall) {
        /*
         *  First, set all CPUs not in this cpuset as used
         */
        cpuset_query (cp, current);
        cpuset_getcpus (cp, used);
        bitmask_complement (used, used);
    }

    while ((dp = readdir (dirp))) {
        char name [4096];

        if (*dp->d_name == '.')
            continue;

        /*
         *  Skip any orphans
         */
        if (strncmp (dp->d_name, "orphan:", 7) == 0)
           continue;

        /*
         *  Generate cpuset name relative to /dev/cpuset
         */
        snprintf (name, sizeof (name), "%s/%s", current, dp->d_name);
        if (cpuset_query (cp, name) < 0) 
            continue;

        if (cpuset_getcpus (cp, b) < 0)
            cpuset_error ("Failed to get CPUs for %s: %m", name);

        used = bitmask_or (used, b, used);
    }
    closedir (dirp);

    bitmask_free (b);
    return (used);
}

int slurm_jobid_is_valid (int jobid)
{
    static job_info_msg_t *msg = NULL;
    int i;

    dyn_slurm_open ();

    cpuset_debug ("slurm_jobid_is_valid (%d)\n", jobid);

    if (msg == NULL) 
        slurm_load_jobs (0, &msg, SHOW_DETAIL);
    else if (jobid == -1) {
        slurm_free_job_info_msg (msg);
        return (0);
    }

    for (i = 0; i < msg->record_count; i++) {
        job_info_t *j = &msg->job_array[i];

        if (j->job_id == jobid && j->job_state == JOB_RUNNING) 
            return (1);
    }

    return (0);
}

int cpuset_ntasks (const char *path)
{
    struct cpuset_pidlist *pids;
    int n;
   
    if ((pids = cpuset_init_pidlist (path, 0)) == NULL) {
        cpuset_error ("cpuset_init_pidlist %s: %m", path);
        return (-1);
    }

    n = cpuset_pidlist_length (pids);

    cpuset_freepidlist (pids);

    return (n);
}

int slurm_cpuset_clean_path (const char *path)
{
    int userid;
    int jobid;
    int stepid;
    const char *name = cpuset_path_to_name (path);

    if (sscanf (name, "/slurm/%d/%d/%d", &userid, &jobid, &stepid) == 2) {
        /*
         *  We only destroy jobid cpusets when the owner uid
         *   cpuset is also empty. This is because the jobid 
         *   cpusets are used for accounting the CPUs in the
         *   uid cpuset.
         */
        char user_cpuset [128];
        snprintf (user_cpuset, sizeof (user_cpuset), "/slurm/%d", userid);
        if ((cpuset_ntasks (user_cpuset) > 0) && 
            slurm_jobid_is_valid (jobid))
            return (0);
    }

    rmdir (path);
    return (0);
}

int slurm_cpuset_clean (cpuset_conf_t cf)
{
    struct cpuset_fts_tree *fts;
    const struct cpuset_fts_entry *entry;

    if (!(fts = cpuset_fts_open ("/slurm")))
        return (-1);
    /*
     *  Reverse cpuset fts tree so that child cpusets
     *   are returned before parents. This is important
     *   because a cpuset can seemingly only be removed
     *   after all its children have been removed.
     */

    cpuset_fts_reverse (fts);

    while ((entry = cpuset_fts_read (fts))) {
        const char *name = cpuset_fts_get_path (entry);


        if (strcmp (name, "/slurm") != 0) {
            char path [4096];
            snprintf (path, sizeof (path), "/dev/cpuset%s", name);
            cpuset_debug ("clean: %s\n", name);
            slurm_cpuset_clean_path (path);
        }
    }

    cpuset_fts_close (fts);

    update_user_cpusets (cf);

    return (0);
}

static int do_cpuset_lock (const char *name)
{
    int fd;
    char path [1024];

    /*
     *  We can't just any files under the cpuset for advisory locking as
     *   we used to do. Recall that the advisory lock is dropped for the 
     *   process if _any_ open file descriptor for the locked file is closed,
     *   Since libcpuset opens and closes *all* files under all our cpusets,
     *   we instead use a more typical lockfile under /var/lock.
     */
    snprintf (path, sizeof (path), "/var/lock/%s-cpuset", name);
        
again:
    if ((fd = open (path, O_RDWR|O_CREAT|O_NOFOLLOW, 0644)) < 0) {
        static int first = 1;
        if (errno == EEXIST && first) { /* A symlink */
            unlink (path);
            first = 0;
            goto again;
        }
        log_err ("Open of lockfile [%s] failed: %s\n", path, strerror (errno));
        return (-1);
    }
    if (fd_get_writew_lock (fd) < 0) {
        close (fd);
        return (-1);
    }
    return (fd);
}

static int do_cpuset_unlock (int fd)
{
    if (fd < 0)
        return (-1);
    /*if (fd_release_lock (fd) < 0)
        return (-1); */
    return (close (fd));
}

int slurm_cpuset_lock (void)
{
    return (do_cpuset_lock ("/slurm"));
}

int slurm_cpuset_unlock (int fd)
{
    return (do_cpuset_unlock (fd));
}

/*
 *   Create slurm cpuset if necessary and return 
 *    with lock held.
 */
static int create_and_lock_cpuset_dir (cpuset_conf_t cf, const char *name)
{
    char path [1024] = "/dev/cpuset";
    struct cpuset *cp;
    int fd;
    mode_t oldmask = umask (022);

    strncat (path, name, sizeof (path));

    cpuset_debug2 ("create_and_lock_cpuset_dir (%s)\n", name);

    /*
     *  First grab cpuset lock from /var/lock:
     */
    if ((fd = do_cpuset_lock (name)) < 0) {
        cpuset_error ("Failed to lock %s: %m", path);
        return (-1);
    }

    if ((mkdir (path, 0755)) < 0) {
        /* If mkdir fails with EEXIST, then slurm cpuset already
         *  exists and we can simply return lockfd after ensuring
         *  the cpuset is "clean"
         */
        umask (oldmask);
        if (errno == EEXIST) {
            slurm_cpuset_clean (cf);
            return (fd);
        }
        else {
            cpuset_error ("mkdir %s: %m", path);
            return (-1);
        }
    } 
    umask (oldmask);

    /*
     *  Initialize SLURM cpuset with all CPUs and MEMs:
     */
    cp = cpuset_alloc ();
    if (cpuset_query (cp, "/") < 0) {
        cpuset_error ("Failed to query root cpuset: %m");
        return (-1);
    }

    cpuset_debug2 ("modifying %s cpuset\n", name);

    if (cpuset_modify (name, cp) < 0) {
        cpuset_error ("Failed to modify %s cpuset: %m", name);
        return (-1);
    }

    cpuset_free (cp);

    return (fd);
}

int slurm_cpuset_create (cpuset_conf_t cf)
{
    return (create_and_lock_cpuset_dir (cf, "/slurm"));
}

int str2int (const char *str)
{
    char *p;
    long l = strtol (str, &p, 10);

    if (p && (*p != '\0'))
        return (-1);

    return ((int) l);
}
/*
 * vi: ts=4 sw=4 expandtab
 */
