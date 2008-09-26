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
#include <sys/personality.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(no-randomize, 1);

#define ADDR_NO_RANDOMIZE 0x0040000

static int default_randomize = 0;
static int randomize = -1;

#define OPT_RANDOMIZE	  1
#define OPT_NO_RANDOMIZE  2

static int process_opts (int val, const char *optarg, int remote);

/*
 *  Provide options to srun:
 */
struct spank_option spank_options[] =
{
    { "addr-randomize", NULL, 
      "Enable address space randomization", 0, OPT_RANDOMIZE,
        (spank_opt_cb_f) process_opts
    },
    { "no-addr-randomize", NULL, 
      "Disable address space randomization", 0, OPT_NO_RANDOMIZE,
        (spank_opt_cb_f) process_opts
    },
    SPANK_OPTIONS_TABLE_END
};


/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    int i;

    for (i = 0; i < ac; i++) {
        if (strncmp ("default_randomize=", av[i], 8) == 0) {
            const char *optarg = av[i] + 18;
            if (*optarg == '0')
                default_randomize = 0;
            else if (*optarg == '1')
                default_randomize = 1;
            else
                slurm_error ("no-randomize: Ignoring invalid default value: "
                        "\"%s\"", av[i]);
        }
        else {
            slurm_error ("no-randomize: Invalid option \"%s\"", av[i]);
        }
    }

    randomize = default_randomize;

    return (0);
}

static int process_opts (int val, const char *optarg, int remote)
{
    if (val == OPT_RANDOMIZE)
        randomize = 1;
    else if (val == OPT_NO_RANDOMIZE)
        randomize = 0;
    else
        randomize = default_randomize;

    return (0);
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    if (randomize == -1)
        randomize = default_randomize;

    slurm_info ("randomize = %d\n", randomize);

    if (randomize == 0 && (personality (ADDR_NO_RANDOMIZE) < 0))
        slurm_error ("Failed to set personality: %m");
    return 0;
}

