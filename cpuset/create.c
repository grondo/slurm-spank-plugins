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


#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <signal.h>

#include "log.h"
#include "conf.h"
#include "create.h"
#include "util.h"
#include "nodemap.h"

/*
 *  Return the /dev/cpuset relative path for job, step, or task [id].
 *    Basically if we're in / or /slurm, return "/slurm/<uid>/<jobid>"
 *     otherwise return <current path>/<id>.
 */
static int job_cpuset_path (uint32_t id, uid_t uid, char *path, int len)
{
    int n;
    char buf [64];

    if (cpuset_getcpusetpath (0, buf, sizeof (buf)) < 0)
        return (-1);

    /*
     *  If we are in root or /slurm cpuset, prepend path to user cpuset
     */
    if (strcmp (buf, "/") == 0 || strcmp (buf, "/slurm") == 0) 
        snprintf (buf, sizeof (buf), "/slurm/%d", uid);

    n = snprintf (path, len, "%s/%u", buf, id);
    if ((n < 0) || (n >= len))
        return (-1);

    return (0);
}

/*
 *  Return a struct cpuset with cpus set to those in  [alloc] and
 *   memory constrained to local memories if constrain_mems == 1.
 */
static struct cpuset * 
do_cpuset_create (cpuset_conf_t cf, const struct bitmask *alloc)
{
    struct cpuset *cp;
    struct bitmask *mems;

    if ((cp = cpuset_alloc ()) == NULL) {
        cpuset_error ("Failed to alloc job cpuset: %m");
        return (NULL);
    }

    if (cpuset_setcpus (cp, alloc) < 0) {
        cpuset_error ("Failed to set cpus: %m");
        goto fail1;
    }

    if ((mems = bitmask_alloc (cpuset_mems_nbits ())) == NULL) {
        cpuset_error ("failed to alloc mems bitmask: %m");
        goto fail1;
    }

    if (cpuset_conf_constrain_mem (cf)) {
        if (cpuset_localmems (alloc, mems) < 0) {
            cpuset_error ("cpuset_localmems failed: %m");
            goto fail2;
        }
    } else {
        if (cpuset_getmems (NULL, mems) < 0)  {
            cpuset_error ("cpuset_getmems: %m");
            goto fail2;
        }
    }

    if (cpuset_setmems (cp, mems) < 0) {
        cpuset_error ("cpuset_setmems failed: %m");
        goto fail2;
    }

    cpuset_set_iopt (cp, "notify_on_release", 1);

    bitmask_free (mems);
    return (cp);

fail2:
    bitmask_free (mems);
fail1:
    cpuset_free (cp);
    return (NULL);
}

int job_cpuset_exists (uint32_t jobid, uid_t uid)
{
    char path [4096];
    struct cpuset *cp;
    int rc;

    if (job_cpuset_path (jobid, uid, path, sizeof (path)) < 0) {
        cpuset_error ("Failed to geneerate job cpuset path\n");
        return (0);
    }

    cp = cpuset_alloc ();
    rc = cpuset_query (cp, path);
    cpuset_free (cp);

    return (rc == 0);
}

/*
 *  Create a job cpuset for job [jobid] user [uid] with cpus in [alloc]
 */
static int 
job_cpuset_create (cpuset_conf_t cf, uint32_t jobid, uid_t uid, 
        const struct bitmask *alloc)
{
    int rc;
    struct cpuset *cp;
    char path [4096];
    mode_t oldmask;

    if ((cp = do_cpuset_create (cf, alloc)) < 0)
        return (-1);

    if (job_cpuset_path (jobid, uid, path, sizeof (path)) < 0) {
        cpuset_error ("Failed to generate job cpuset path: %s\n", 
                strerror (errno));
        goto out;
    }

    oldmask = umask (022);
    if (cpuset_create (path, cp) < 0)
        cpuset_error ("create [%s]: %s", path, strerror (errno));
    else
        rc = 0;
    umask (oldmask);

    print_cpuset_info (path, cp);

out:
    cpuset_free (cp);
    return (rc);
}

#if 0
static struct bitmask * cpuset_cpus_bitmask (const char *name)
{
    struct cpuset *cp = cpuset_alloc ();
    struct bitmask *b = NULL;

    if (cpuset_query (cp, name) < 0) {
        cpuset_error ("cpuset query %s: %m", name);
        goto out;
    }

    if ((b = bitmask_alloc (cpumask_size ())) == NULL) {
        cpuset_error ("bitmask_alloc: %m");
        goto out;
    }

    if (cpuset_getcpus (cp, b) < 0) {
        cpuset_error ("Failed to get cpus for cpuset %s: %m", name);
        bitmask_free (b);
        b = NULL;
    }
out:
    cpuset_free (cp);
    return (b);
}
#endif

/*
 * Create a cpuset for [id] user [uid] with ncpus.
 */
static int 
create_cpuset (cpuset_conf_t cf, unsigned int id, uid_t uid, int ncpus)
{
    struct nodemap *map;
    struct bitmask *alloc;
    int rc = -1;

    if (!(map = nodemap_create (cf, NULL)))
        return (-1);

    if ((alloc = nodemap_allocate (map, ncpus)) == NULL) 
        goto out;

    /*
     *  Create and/or update user cpuset, under which job cpuset will
     *   be created.
     */
    if ((int) uid >= 0) {
        cpuset_debug ("Updating user %d cpuset with %d cpus\n", uid, ncpus);
        if (user_cpuset_update (cf, uid, alloc) < 0) {
            cpuset_error ("Failed to update user cpuset");
            goto out;
        }
    }

    if (job_cpuset_create (cf, id, uid, alloc) < 0) 
        goto out;

    rc = 0;
out:
    if (map)
        nodemap_destroy (map);
    if (alloc)
        bitmask_free (alloc);

    if (rc < 0)
        log_debug2 ("create_cpuset: id=%u uid=%d ncpus=%d: Failed.\n",
                id, uid, ncpus);

    return (rc);
}

int create_cpuset_for_job (cpuset_conf_t cf, unsigned jobid, uid_t uid, 
        int ncpus)
{
    return (create_cpuset (cf, jobid, uid, ncpus));
}

int create_cpuset_for_step (cpuset_conf_t cf, unsigned int stepid, int ncpus)
{
    return (create_cpuset (cf, stepid, -1, ncpus));
}

int create_cpuset_for_task (cpuset_conf_t cf, unsigned int taskid, int ncpus)
{
    return (create_cpuset (cf, taskid, -1, ncpus));
}

static int user_cpuset_orphan (uid_t uid, const char *path)
{
    char orphan [1024];
    int n;
    n = snprintf (orphan, sizeof (orphan), "/dev/cpuset/slurm/orphan:%d", uid);
    if ((n <= 0) || (n > sizeof (orphan)))
        return (-1);
    if (rename (path, orphan) < 0)
        cpuset_error ("Failed to rename %s to %s: %m", path, orphan);
    return (0);
}

static int kill_orphan (const char *name)
{
    struct cpuset_pidlist *pids;
    int i;

    if ((pids = cpuset_init_pidlist (name, 0)) == NULL) {
        cpuset_error ("cpuset_init_pidlist: %s: %s\n", 
                name, strerror (errno));
        return (-1);
    }

    for (i = 0; i < cpuset_pidlist_length (pids); i++)
        kill (cpuset_get_pidlist (pids, i), SIGKILL);

    cpuset_freepidlist (pids);
    return (0);
}

static int user_cpuset_unorphan (uid_t uid, const char *path)
{
    char orphan [1024];
    int n;
    n = snprintf (orphan, sizeof (orphan), "/dev/cpuset/slurm/orphan:%d", uid);
    if ((n <= 0) || (n > sizeof (orphan)))
        return (-1);
    cpuset_debug ("rename (%s, %s)\n", orphan, path);
    if (rename (orphan, path) < 0)
        return (0);
    return (1);
}

/*
 *  If user cpuset does not exist, keep its cpus and mems empty
 *   They'll be filled in later.
 */
static int user_cpuset_create (const char *path)
{
    int rc = 0;
    mode_t oldmask = umask (022);

    if ((mkdir (path, 0755)) < 0 && errno != EEXIST) {
        cpuset_error ("mkdir %s: %m", path);
        rc = -1;
    }
    umask (oldmask);
    return (rc);
}


int 
user_cpuset_update (cpuset_conf_t cf, uid_t uid, const struct bitmask *alloc)
{
    int rc = -1;
    char path [1024];
    const char *name;
    struct bitmask *used;
    struct cpuset *cp;
    int orphan = 0;

    snprintf (path, sizeof (path), "/dev/cpuset/slurm/%d", uid);
    name = cpuset_path_to_name (path);

    /*
     *  If there is an orphan user login, move it back 
     *   Otherwise, create regular user cpuset if it doesn't
     *   already exist.
     */
    if (!(orphan = user_cpuset_unorphan (uid, path))
       && (user_cpuset_create (path) < 0))
        return (-1);

    cpuset_debug ("Updating user cpuset at %s\n", path);
    used = used_cpus_bitmask_path (path, 1);
    if (orphan)
        bitmask_clearall (used);
    if (alloc) 
        bitmask_or (used, used, alloc);

    if (bitmask_weight (used) == 0) {
        /*
         *  This is an orphaned user cpuset. 
         *   We can't leave it with 0 cpus, and
         *   we can't leave it allocated under /slurm
         *   since those cpusets are used for tracking
         *   in-use cpusets. Instead, just rename the
         *   current cpuset to an orphans directory.
         */
        cpuset_debug ("user_cpuset_orphan: uid=%d\n", uid);
        if (cpuset_conf_kill_orphans (cf))
            kill_orphan (name);
        else
            user_cpuset_orphan (uid, path);
        return (0);
    }

    if (!(cp = do_cpuset_create (cf, used))) {
        bitmask_free (used);
        return (-1);
    }

again:
    if ((rc = cpuset_modify (name, cp)) < 0) {
        /*
         *  cpuset_modify can potentially return EBUSY.
         */
        if (errno == EBUSY || errno == EAGAIN) {
            sleep (1);
            goto again;
        }
        cpuset_error ("Failed to modify %s: %m", name);
    }

    bitmask_free (used);
    cpuset_free (cp);
    return (rc);
}

int update_user_cpusets (cpuset_conf_t cf)
{
    DIR *dirp;
    struct dirent *dp;

    if ((dirp = opendir ("/dev/cpuset/slurm")) == NULL) {
        cpuset_error ("Unable to open /dev/cpuset/slurm: %m");
        return (-1);
    }

    while ((dp = readdir (dirp))) {
        int uid;
        if ((uid = str2int (dp->d_name)) < 0)
            continue;
        cpuset_debug ("Checking cpuset for uid %d\n", uid);
        user_cpuset_update (cf, uid, NULL);
    }
    closedir (dirp);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
