/***************************************************************************** *
 *  Copyright (C) 2009 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Jim Garlick <garlick@llnl.gov>.
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

/* private-mount.c - mount fs from /etc/slurm/fstab privately for job/task */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pwd.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <mntent.h>
#include <slurm/spank.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <errno.h>

#include "lib/list.h"
#include "lib/split.h"

#define PATH_REG_FSTAB "/etc/fstab"
#define PATH_ALT_FSTAB "/etc/slurm/fstab"
#define PATH_MOUNT     "/bin/mount"

#define MOUNTPT_MODE    0755

SPANK_PLUGIN(mount, 1)

#define PROG "private-mount"

static int _opt_fs_namespace (int val, const char *optarg, int remote);
static int _opt_private_mount (int val, const char *optarg, int remote);

typedef enum { NS_SYSTEM, NS_TASK, NS_JOB } mode_t;

static mode_t ns_mode   = NS_SYSTEM;
static List mounts      = NULL;
static int allowed      = 0;

struct spank_option spank_options[] =
{
    { "private-mount", "[dirs]",
        "Mount comma-separated file system [dirs] privately.",
		1, 0, (spank_opt_cb_f) _opt_private_mount },
    { "fs-namespace", "[task|job]", 
        "Create private file system namespace for each [task|job].",
		1, 0, (spank_opt_cb_f) _opt_fs_namespace },
    SPANK_OPTIONS_TABLE_END
};

static int _runcmd(char *cmd, char *av[])
{
    int s;
    pid_t pid;

    switch ((pid = fork())) {
        case -1:
            slurm_error ("%s: fork: %m", PROG);
            return (-1);
        case 0:
            execv (cmd, av);
            exit (1);
        default:
            if (waitpid (pid, &s, 0) < 0) {
                slurm_error ("%s: wait: %m", PROG);
                return (-1);
            }
            if (!WIFEXITED (s) || WEXITSTATUS (s) != 0) {
                slurm_error ("%s: %s failed (%d)", PROG, cmd, s);
                return (-1);
            }
            break;
    }

    return (0);
}

static int _mount_n (char *path)
{
    char *args[] = { "mount", "-n", path, NULL };

    return _runcmd (PATH_MOUNT, args);
}

static int _lookup (char *name)
{
    FILE *fp;
    struct mntent *mnt;
    int rc = -1;

    if ((fp = setmntent (PATH_ALT_FSTAB, "r"))) {
        while ((mnt = getmntent (fp)))
            if (!strcmp (mnt->mnt_dir, name))
                rc = 0;
        endmntent (fp);
    }

    return (rc);
}

static int _mkdir_p (char *path, mode_t mode)
{
    char *cpy;
    int rc;
    mode_t saved_umask;
    struct stat sb;

    saved_umask = umask(022);
    if ((rc = mkdir (path, mode)) < 0) {
        switch (errno) {
            case ENOENT:
                if (!(cpy = strdup (path))) {
                    errno = ENOMEM;
                    rc = -1;
                    break;
                }
                if ((rc = _mkdir_p (dirname (cpy), mode)) == 0)
                    rc = mkdir (path, mode);
                free (cpy);
                break;
            case EEXIST:
                if (stat (path, &sb) == 0 && S_ISDIR(sb.st_mode))
                    rc = 0;
                break;
        }
    }
    umask(saved_umask);

    return rc;
}

static int _private_mount (void)
{
    struct statfs sfs;
    ListIterator itr;
    char *name;
    int rc = 0;

    if (unshare (CLONE_NEWNS) < 0) {
        slurm_error ("%s: unshare CLONE_NEWNS: %m", PROG);
        return (-1);
    }

    if (mounts) {
        if (mount (PATH_ALT_FSTAB, PATH_REG_FSTAB, NULL, MS_BIND, NULL) < 0) {
            slurm_error ("%s: private mount --bind %s %s: %m",
                         PROG, PATH_ALT_FSTAB, PATH_REG_FSTAB);
            return (-1);
        }
        if (!(itr = list_iterator_create (mounts))) {
            slurm_error ("%s: out of memory", PROG);
            return (-1);
        }
        while ((name = list_next (itr))) {
            if (_lookup (name) < 0) {
                slurm_error ("%s: no fstab entry: %s\n", PROG, name);
                rc = -1;
                break;
            }
            if (_mkdir_p (name, MOUNTPT_MODE) < 0) {
                slurm_error ("%s: mkdir -p %s: %m\n", PROG, name);
                rc = -1;
                break;
            }
            if (_mount_n (name) < 0) {
                slurm_error ("%s: mount -n %s: %m\n", PROG, name);
                rc = -1;
                break;
            }
            /* Lustre hack: statfs normally blocks until all server connections
             * are established.  Do it here to avoid racing setup and teardown
             * for very short jobs.
             */
            if (statfs (name, &sfs) < 0) {
                slurm_error ("%s: statfs %s: %m\n", PROG, name);
                rc = -1;
                break;
            }
        }
        list_iterator_destroy (itr);
    }

    return rc;
}

int slurm_spank_task_init_privileged (spank_t sp, int ac, char **av)
{
    /* N.B. Only called in remote context */
    if (ns_mode != NS_TASK)
        return (0);

    if (!allowed) {
        slurm_error ("%s: not authorized to use plugin", PROG);
        return (-1);
    }

    return _private_mount ();
}

int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
    /* N.B. Called in both local and remote context */
    if (!spank_remote (sp) || ns_mode != NS_JOB)
        return (0);

    if (!allowed) {
        slurm_error ("%s: not authorized to use plugin", PROG);
        return (-1);
    }

    return _private_mount ();
}

int slurm_spank_exit(spank_t sp, int ac, char **av)
{
    if (mounts) {
        list_destroy(mounts);
        mounts = NULL;
    }

    return (0);
}

int slurm_spank_init(spank_t sp, int ac, char **av)
{
    int i;
    uid_t uid;
    struct passwd *pw;
    List u = NULL;
    ListIterator itr = NULL;
    char *cpy = NULL, *user;
    int rc = 0;

    for (i = 0; i < ac; i++) {
        if (strncmp("allowuser=", av[i], 10) == 0) {
            if (u) {
                slurm_error ("%s: only one allowuser option allowed."
                             " Use commas to separate users.", PROG);
                rc = -1;
                goto done;
            }
            if (!(cpy = strdup(av[i] + 10)) || !(u = list_split (",", cpy))) {
                slurm_error ("%s: out of memory", PROG);
                rc = -1;
                goto done;
            }
        } else {
            slurm_error ("%s: invalid argument", PROG);
            rc = -1;
            goto done;
        }
    }

    if (u) {
        if (spank_remote (sp)) {
            if (spank_get_item (sp, S_JOB_UID, &uid) != ESPANK_SUCCESS) {
                slurm_error ("%s: could not obtain job uid", PROG);
                rc = -1;
                goto done;
            }
        } else {
            uid = getuid ();
        }
        if (!(itr = list_iterator_create (u))) {
            slurm_error ("%s: out of memory", PROG);
            rc = -1;
            goto done;
        }
        while ((user = list_next(itr))) {
            if (!(pw = getpwnam(user))) {
                slurm_error ("%s: warning: allowuser=%s\n", PROG, user);
                continue;
            }
            if (uid == pw->pw_uid) {
                allowed = 1;
                break;
            }
        }
    }
done:
    if (cpy)
        free(cpy);
    if (itr)
        list_iterator_destroy(itr);
    if (u)
        list_destroy(u);

    return (rc);
}

static int _opt_fs_namespace (int val, const char *optarg, int remote)
{
    if (!allowed) {
        slurm_error ("%s: not authorized to use plugin", PROG);
        return (-1);
    }
    if (optarg == NULL) {
        slurm_error ("%s: --fs-namespace: option requires argument", PROG);
        return (-1);
    }
    if (!strcmp (optarg, "task")) {
        ns_mode = NS_TASK;
    } else if (!strcmp (optarg, "job")) {
        ns_mode = NS_JOB;
    } else {
        slurm_error ("%s: --fs-namespace: invalid argument", PROG);
        return (-1);
    }

    return (0);
}

static int _opt_private_mount (int val, const char *optarg, int remote)
{
    ListIterator itr;
    char *name, *cpy;
    int rc = 0;

    if (!allowed) {
        slurm_error ("%s: not authorized to use plugin", PROG);
        return (-1);
    }
    if (optarg == NULL) {
        slurm_error ("%s: --private-mount: option requires argument", PROG);
        return (-1);
    }
    if (mounts != NULL) {
        slurm_error ("%s: --private-mount: option can be used only once", PROG);
        return (-1);
    }

    if (ns_mode == NS_SYSTEM)
        ns_mode = NS_JOB;

    if (!(cpy = strdup(optarg)) || !(mounts = list_split (",", cpy))) {
        slurm_error ("%s: out of memory", PROG);
        rc = -1;
        goto done;
    }
    if (!(itr = list_iterator_create (mounts))) {
        slurm_error ("%s: out of memory", PROG);
        rc = -1;
        goto done;
    }
    while ((name = list_next (itr))) {
        if (_lookup ((char *)name) < 0) {
            slurm_error ("%s: no fstab entry: %s", PROG, optarg);
            rc = -1;
        }
    }
done:
    if (cpy)
        free (cpy);
    if (itr)
        list_iterator_destroy (itr); 
    if (mounts && rc == -1) {
        list_destroy (mounts);
        mounts = NULL;
    }

    return (rc);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
