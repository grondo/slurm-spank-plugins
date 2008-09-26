/*****************************************************************************
 *
 *  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
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

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <pwd.h>
#include <sched.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(iorelay, 1)

#define IORELAY_ENABLE  1

/* Usage: iorelay-mount-nodezero -u user -m mntpt */
#define MOUNT_SCRIPT    "/usr/libexec/iorelay-mount-nodezero"

/* Usage: iorelay-bind-nfs -m mntpt */
#define BIND_SCRIPT     "/usr/libexec/iorelay-bind-nfs"

static int enabled = 0;

static int _opt_process (int val, const char *optarg, int remote);

/*
 *  Provide a --iorelay option to srun:
 */
struct spank_option spank_options[] =
{
    { "iorelay", NULL, "Enable NFS I/O relaying.", 
		1, IORELAY_ENABLE, 
		(spank_opt_cb_f) _opt_process
    },
    SPANK_OPTIONS_TABLE_END
};

/* 
 * Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    char cmd[256];
    struct passwd *pw;
    uid_t uid;

	if (!enabled || !spank_remote (sp))
		return (0);
   
    spank_get_item (sp, S_JOB_UID, &uid);
    pw = getpwuid (uid);
    if (!pw) {
        slurm_error ("Error looking up uid in /etc/passwd");
        return (-1);
    }

    /* Unshare file namespace.  This means only this process and its children
     * will see the following mounts, and when this process and its children
     * terminate, the mounts go away automatically.
     */
    if (unshare (CLONE_NEWNS) < 0) {
        slurm_error ("unshare CLONE_NEWNS: %m");
        return (-1);
    }

    /* Mount node zero root on /mnt using sshfs.
     * Script has no effect on node zero.
     */
    snprintf (cmd, sizeof(cmd), "%s -u %s -m /mnt", MOUNT_SCRIPT, pw->pw_name);
    if (system (cmd) != 0) {
        slurm_error ("Error running `%s': %m", cmd);
        return (-1);
    }

    /* Bind NFS-mounted directories now mirrored in /mnt via sshfs
     * over their NFS mount points.
     * Script has no effect on node zero.
     */
    snprintf (cmd, sizeof(cmd), "%s -m /mnt", BIND_SCRIPT);
    if (system (cmd) != 0) {
        slurm_error ("Error running `%s': %m", cmd);
        return (-1);
    }

	return (0);
}

/* 
 * Called from both srun and slurmd.
 */
int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    /* Do nothing here as mounts in private namespace will take care of 
     * themselves.  
     */
    return (0);
}

static int _opt_process (int val, const char *optarg, int remote)
{
    switch (val) {
        case IORELAY_ENABLE:
            enabled = 1;
            break;
        default:
            slurm_error ("Ignoring unknown iorelay option value %d\n", val);
            break;
	} 

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
