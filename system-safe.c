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

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(system-safe, 1)

#define SYSTEM_SAFE_ENABLE  0x0
#define SYSTEM_SAFE_DISABLE 0x1

/*  
 *  Disabled by default
 */
static int enabled = 0;
static int opt_enable = 0;
static int opt_disable = 0;

static int _opt_process (int val, const char *optarg, int remote);

/*
 *  Provide a --renice=[prio] option to srun:
 */
struct spank_option spank_options[] =
{
    { "system-safe",    NULL, "Replace system(3) with version safe for MPI.", 
		0, SYSTEM_SAFE_ENABLE, 
		(spank_opt_cb_f) _opt_process
    },
	{ "no-system-safe", NULL, "Disable system(3) replacement.", 
		0, SYSTEM_SAFE_DISABLE,
        (spank_opt_cb_f) _opt_process
	},
    SPANK_OPTIONS_TABLE_END
};


/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    int i;

	if (!spank_remote (sp))
		return (0);

    for (i = 0; i < ac; i++) {
        if (strncmp ("enabled", av[i], 7) == 0) {
			enabled = 1;
        }
        else if (strncmp ("disabled", av[i], 8) == 0) {
			enabled = 0;
        }
        else {
            slurm_error ("system-safe: Invalid option \"%s\"", av[i]);
        }
    }

    return (0);
}

int slurm_spank_user_init (spank_t sp, int ac, char **av)
{
	char buf [4096];
	const char *preload = "system-safe-preload.so";

	if (opt_disable || (!enabled && !opt_enable))
		return (0);

	if (spank_getenv (sp, "LD_PRELOAD", buf, sizeof (buf)) == ESPANK_SUCCESS) 
		snprintf (buf, sizeof (buf), "%s %s", buf, preload);
	else
		strncpy (buf, preload, strlen (preload));

	if (spank_setenv (sp, "LD_PRELOAD", buf, 1) != ESPANK_SUCCESS)
		slurm_error ("Failed to set LD_PRELOAD=%s\n", buf);

	return (0);
}

static int _opt_process (int val, const char *optarg, int remote)
{
	if (val == SYSTEM_SAFE_ENABLE)
		opt_enable = 1;
	else 
		opt_disable = 0;

    return (0);
}



/*
 * vi: ts=4 sw=4 expandtab
 */
