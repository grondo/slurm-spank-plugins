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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <slurm/spank.h>

SPANK_PLUGIN (tmpdir, 1);

/*
 * Create job-specific TMPDIR.
 *  Called from srun after allocation before launch.
 *  Does the equivalent of TMPDIR=${TMPDIR-/tmp}/$SLURM_JOBID.$SLURM_STEPID
 */
int slurm_spank_local_user_init (spank_t sp, int ac, char **av)
{
    uint32_t jobid, stepid;
    const char *tmpdir;
    char buf [1024];
    int n;

    if (spank_get_item (sp, S_JOB_ID, &jobid) != ESPANK_SUCCESS) {
        slurm_error ("Failed to get jobid from SLURM");
        return (-1);
    }

    if (spank_get_item (sp, S_JOB_STEPID, &stepid) != ESPANK_SUCCESS) {
        slurm_error ("Failed to get job step id from SLURM");
        return (-1);
    }

    if (!(tmpdir = getenv ("TMPDIR")))
        tmpdir = "/tmp";

    n = snprintf (buf, sizeof (buf), "%s/%u.%u", tmpdir, jobid, stepid);

    if ((n < 0) || (n > sizeof (buf) - 1)) {
        slurm_error ("TMPDIR = \"%s\" too large. Aborting");
        return (-1);
    }

    if (setenv ("TMPDIR", buf, 1) < 0) {
        slurm_error ("setenv (TMPDIR, \"%s\"): %m", buf);
        return (-1);
    }

    return (0);
}

/*
 * ``rm -rf TMPDIR'' *as user* after job tasks have exited
 */
int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    const char sudo [] = "/usr/bin/sudo -u";
    const char rm []   = "/bin/rm -rf";
    char tmp [1024];
    char cmd [4096];
    int n;
    int status;
    uid_t uid = (uid_t) -1;

    if (!spank_remote (sp))
        return (0);

    if (spank_getenv (sp, "TMPDIR", tmp, sizeof (tmp)) != ESPANK_SUCCESS) {
        slurm_error ("Unable to remove TMPDIR at exit!");
        return (-1);
    }

    if (spank_get_item (sp, S_JOB_UID, &uid) != ESPANK_SUCCESS) {
        slurm_error ("tmpdir: Unable to get job's user id");
        return (-1);
    }

    n = snprintf (cmd, sizeof (cmd), "%s \\#%d %s %s", sudo, uid, rm, tmp);

    if ((n < 0) || (n > sizeof (cmd) - 1)) {
        slurm_error ("Unable to remove TMPDIR at exit!");
        return (-1);
    }

    if ((status = system (cmd)) != 0) {
        slurm_error ("\"%s\" exited with status=0x%04x\n", cmd, status);
        return (-1);
    }

    return (0);
}
