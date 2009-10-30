/***************************************************************************\
 * setsched.c - Spank Plugin to enforce a particular kernel scheduling policy
 ***************************************************************************
 *
 * Copyright CEA/DAM/DIF (2009)
 *
 * Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *
 * This file is part of slurm-spank-plugins, a set of spank plugins 
 * for SLURM.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
\***************************************************************************/

/*
 * To compile it : gcc -fPIC -shared -o setsched.so setsched.c
 *
 * This plugin can be used to enforce a particular scheduling policy
 * as well as the associated priority of tasks spawned by slurm.
 *
 * The following configuration parameters are available on server side :
 *
 * policy   : set the kernel scheduling policy to use (default is 0)
 * priority : set the priority to configure with the policy (default is 0)
 * default  : set setsched plugin default behavior i.e. enabled/disabled
 *
 * Users can alter the enabled/disabled behavior on command line using 
 * --setsched
 *
 * setsched can be used only if at least one of the policy or priority 
 * parameters are set to a non-zero value
 *
 * Here is an example of configuration :
 *
 * optional setsched.so policy=55 priority=0 default=disabled
 *
*/

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/resource.h>
#include <limits.h>

#ifdef _POSIX_PRIORITY_SCHEDULING
#include <sched.h>
#else
#warning "no Posix priority scheduling primitives detected on this system"
#endif

#include <errno.h>
extern int errno;

#include <slurm/spank.h>

#define SPANK_SETSCHED_VERSION "0.1.4"

static int setsched_pol=0;
static int setsched_prio=0;
static int setsched_default=0;

#define xinfo  slurm_info
#define xerror slurm_error
#define xdebug slurm_debug

/*
 * All spank plugins must define this macro for the SLURM plugin loader.
 */
SPANK_PLUGIN(setsched, 1);

static int _str2int (const char *str, int *p2int)
{
	long int l;
	char *p;
	
	l = strtol (str, &p, 10);

	/* check for underflow and overflow */
	if ( l == LONG_MIN || l == LONG_MAX )
		return (-1);

	*p2int = (int) l;

	return (0);
}

static int _setsched_opt_process (int val, const char *optarg, int remote)
{
	if (optarg == NULL) {
		return 0;
	}

	if (strncmp ("no", optarg, 2) == 0) {
	        setsched_default=0;
		xdebug("setsched: disabled on user request",optarg);
	}
	else if (strncmp ("yes", optarg, 3) == 0) {
	        setsched_default=1;
		xdebug("setsched: enabled on user request",optarg);
	}
	else if (strncmp ("auto", optarg, 4) != 0) {
		xerror ("setsched: bad parameter %s", optarg);
		return (-1);
	}

	return (0);
}


/*
 *  Provide a --setsched=[yes|no|auto] option to srun:
 */
struct spank_option spank_options[] =
{
	{ "setsched", "[yes|no|auto]", "Activate/Desactivate scheduling policy "
	  "setting of Setsched spank plugin", 2, 0,
	  (spank_opt_cb_f) _setsched_opt_process
	},
	SPANK_OPTIONS_TABLE_END
};


/*
 *  Called from both srun and slurmd.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
	int i;
	
	int pol=0;
	int prio=0;

	/* do something in remote mode only */
	if ( ! spank_remote(sp) )
		return 0;

	for (i = 0; i < ac; i++) {
		if (strncmp ("policy=", av[i], 7) == 0) {
			const char *optarg = av[i] + 7;
			if (_str2int (optarg, &pol) < 0)
				xerror ("setsched: ignoring invalid policy "
					"value: %s", av[i]);
		}
		else if (strncmp ("priority=", av[i], 9) == 0) {
			const char *optarg = av[i] + 9;
                        if (_str2int (optarg, &prio) < 0)
                                xerror ("setsched: ignoring invalid priority "
					"value: %s", av[i]);

		}
		else if (strncmp ("default=enabled", av[i], 15) == 0) {
		        setsched_default=1;
		}
		else if (strncmp ("default=disabled", av[i], 16) == 0) {
		        setsched_default=0;
		}
		else {
			xerror ("setsched: "
				     "invalid option: %s", av[i]);
		}
	}

	if ( pol > 0 || prio > 0 ) {
		setsched_pol=pol;
		setsched_prio=prio;
		xdebug("setsched: configuration is policy=%d "
		       "priority=%d default=%s (version %s)",
		       setsched_pol,setsched_prio,
		       setsched_default?"enabled":"disabled",
		       SPANK_SETSCHED_VERSION);
	}
	
	return 0;
}

int slurm_spank_task_post_fork (spank_t sp, int ac, char **av)
{
	int status = 0;

	pid_t pid;
	int taskid;

	int pol;
	struct sched_param spar;

	if ( setsched_default && ( setsched_pol > 0 || setsched_prio > 0 ) ) {

		pol=setsched_pol;
		spar.sched_priority=setsched_prio;
		
		spank_get_item (sp, S_TASK_GLOBAL_ID, &taskid);
		spank_get_item (sp, S_TASK_PID, &pid);
		
		status = sched_setscheduler(pid, pol, &spar);
		if (status < 0) {
			xerror("setsched: unable to set scheduling "
			       "policy of task%d pid %ld : %s",
			       taskid, pid,strerror(errno));
		}
		else 
		        xinfo("setsched: "
			      "scheduling policy of task%d pid %ld is "
			      "now %ld (prio=%d)",
			      taskid, pid, pol, setsched_prio);
	}

	return status;
}
