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

#include <unistd.h>
#include <stdlib.h>

#include "use-env.h"
#include "log_msg.h"

extern int yydebug;
static char *run_as_task = NULL;

int get_options (int ac, char **av, char **ppath, char **nnodes, char **nprocs)
{
	int c;

	while ((c = getopt (ac, av, "dvt:f:n:N:")) >= 0) {
		switch (c) {
		case 'd' :
			yydebug = 1;
			break;
		case 'v':
			log_msg_verbose ();
			break;
		case 'f':
			*ppath = optarg;
			break;
		case 'n':
			*nprocs = optarg;
			break;
		case  'N':
			*nnodes = optarg;
			break;
		case 't':
			run_as_task = optarg;
			break;
		case '?' :
		default:
			exit (1);
		}
	}
	return (0);
}


int main (int ac, char **av)
{
	int rc = 0;
	char *filename = NULL;
	char *nnodes = "0";
	char *nprocs = "0";

	log_msg_init ("use-env");

	get_options (ac, av, &filename, &nnodes, &nprocs);

	keyword_define ("SLURM_NNODES", nnodes);
	keyword_define ("SLURM_NPROCS", nprocs);

	if (run_as_task) {
		keyword_define ("SLURM_PROCID", run_as_task);
		keyword_define ("SLURM_NODEID", "0");
	}

	use_env_parser_init (run_as_task != NULL);
	rc = use_env_parse (filename);
	use_env_parser_fini ();
	log_msg_fini ();

	return (rc);
}
