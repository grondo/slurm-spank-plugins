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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(iotrace, 1)

#define IOTRACE_ENABLE  1

static int enabled = 0;
static char *flags = NULL;

static int _opt_process (int val, const char *optarg, int remote);

/*
 *  Provide a --iotrace option to srun:
 */
struct spank_option spank_options[] =
{
    { "iotrace", "[flags]", "Enable application I/O tracing.", 
		2, IOTRACE_ENABLE, 
		(spank_opt_cb_f) _opt_process
    },
    SPANK_OPTIONS_TABLE_END
};


static void _iotrace_label(spank_t sp, char *buf, int len)
{
    char hostname[128], *p;
    uint32_t taskid = 0;
    spank_err_t rc;

    rc = spank_get_item (sp, S_TASK_GLOBAL_ID, &taskid);
    if (rc != ESPANK_SUCCESS)
        slurm_error ("iotrace: error fetching taskid: %d", rc);

    if (gethostname (hostname, sizeof (hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = '\0';
        if ((p = strchr (hostname, '.')))
            *p = '\0';
    } else
        strncpy (hostname, "unknown",  sizeof(hostname));

    snprintf (buf, len, "iotrace-%d@%s", taskid, hostname);
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
	char nbuf [4096], obuf [4096];
	char label [64];
	const char *preload = "libplasticfs.so";
    const char *lflags = flags ? flags : "";

	if (!enabled)
		return (0);

    /* append to LD_PRELOAD (with a space) */
	if (spank_getenv (sp, "LD_PRELOAD", obuf, sizeof (obuf)) == ESPANK_SUCCESS) 
		snprintf (nbuf, sizeof (nbuf), "%s %s", obuf, preload);
	else
		strncpy (nbuf, preload, strlen (preload));
	if (spank_setenv (sp, "LD_PRELOAD", nbuf, 1) != ESPANK_SUCCESS)
		slurm_error ("Failed to set LD_PRELOAD=%s\n", nbuf);

    /* prepend to PLASTICFS (with a pipe) */
    _iotrace_label (sp, label, sizeof (label));
	if (spank_getenv (sp, "PLASTICFS", obuf, sizeof (obuf)) == ESPANK_SUCCESS)
		snprintf (nbuf, sizeof (nbuf), "log -  %s %s | %s",
                label, lflags, obuf);
	else
		snprintf (nbuf, sizeof (nbuf), "log -  %s %s", label, flags);

	if (spank_setenv (sp, "PLASTICFS", nbuf, 1) != ESPANK_SUCCESS)
		slurm_error ("Failed to set PLASTICFS=%s\n", nbuf);

	return (0);
}

static int _opt_process (int val, const char *optarg, int remote)
{
    switch (val) {
        case IOTRACE_ENABLE:
            enabled = 1;
            if (optarg)
                flags = strdup (optarg);
            break;
        default:
            slurm_error ("Ignoring unknown iotrace option value %d\n", val);
            break;
	} 

    return (0);
}

int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    if (flags)
        free (flags);
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
