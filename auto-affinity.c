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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>

#define __USE_GNU
#include <sched.h>

#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "lib/split.h"
#include "lib/fd.h"

SPANK_PLUGIN(auto-affinity, 1);

static int ncpus  = -1;
static int ntasks = -1;
static int enabled = 1;
static int verbose = 0;
static int reverse = 0;
static int startcpu = 0;
static int requested_cpus_per_task = 0;
static int exclusive_only = 0;

static cpu_set_t cpus_available;
static int       ncpus_available;

static const char auto_affinity_help [] = 
"\
auto-affinity: Automatically assign CPU affinity using best-guess defaults.\n\
\n\
The default behavior attempts to accomodate multi-threaded apps by \n\
assigning more than one CPU per task if the number of tasks running \n\
on the node is evenly divisible into the number of CPUs. Otherwise, \n\
CPU affinity is not enabled unless the cpus_per_task (cpt) option is \n\
specified. The default behavior may be modified using the \n\
--auto-affinity options listed below. Also, the srun(1) --cpu_bind option\n\
is processed after auto-affinity, and thus may be used to override any \n\
CPU affinity settings from this module.\n\
  \n\
Option Usage: --auto-affinity=[args...]\n\
  \n\
where args... is a comma separated list of one or more of the following\n\
  help              Display this message.\n\
  v(erbose)         Print CPU affinty list for each remote task\n\
  \n\
  off               Disable automatic CPU affinity.\n\
  \n\
  start=N           Start affinity assignment at CPU [N]. If assigning CPUs\n\
                    in reverse, start [N] CPUs from the last CPU.\n\
  rev(erse)         Allocate last CPU first instead of starting with CPU0.\n\
  cpus_per_task=N   Allocate [N] CPUs to each task.\n\
  cpt=N             Shorthand for cpus_per_task.\n\n";


static int parse_user_option (int val, const char *optarg, int remote);

struct spank_option spank_options [] = {
    { "auto-affinity", "[args]",
      "Automatic, best guess CPU affinity for SMP machines " 
          "(args=`help' for more info)",
      2, 0, (spank_opt_cb_f) parse_user_option 
    },
    SPANK_OPTIONS_TABLE_END
};

static int str2int (const char *str)
{
    char *p;
    long l = strtol (str, &p, 10);

    if (p && (*p != '\0'))
        return (-1);

    return ((int) l);
}

static int parse_option (const char *opt, int *remotep)
{
    if (strcmp (opt, "off") == 0)
        enabled = 0;
    else if ((strcmp (opt, "reverse") == 0) || (strcmp (opt, "rev") == 0))
        reverse = 1;
    else if (strncmp (opt, "cpt=", 4) == 0) {
        if ((requested_cpus_per_task = str2int (opt+4)) < 0) 
            goto fail;
    } 
    else if (strncmp (opt, "cpus_per_task=", 14) == 0) {
        if ((requested_cpus_per_task = str2int (opt+14)) < 0) 
            goto fail;
    } 
    else if (strncmp (opt, "start=", 6) == 0) {
        if ((startcpu = str2int (opt+6)) < 0) 
            goto fail;
    } 
    else if (strcmp (opt, "verbose") == 0 || strcmp (opt, "v") == 0)
        verbose = 1;
    else if ((strcmp (opt, "help") == 0) && !(*remotep)) {
        fprintf (stderr, auto_affinity_help);
        exit (0);
    }
        
    return (0);

    fail:
    slurm_error ("auto-affinity: Invalid option: `%s'", opt);
    return (-1);
}

static int parse_user_option (int val, const char *arg, int remote)
{
    char *str;
    List l;
    int rc = 1;

    if (arg == NULL) 
        return (0);

    l = list_split (",", (str = strdup (arg)));
    rc = list_for_each (l, (ListForF) parse_option, &remote);

    list_destroy (l);
    free (str);

    return (rc);
}

static int parse_argv (int ac, char **av, int remote)
{
    int i;
    for (i = 0; i < ac; i++) {
        if (strcmp (av[i], "off") == 0)
            enabled = 0;
        else if (strcmp (av[i], "exclusive_only") == 0)
            exclusive_only = 1;
        else 
            return (-1);
    }
    return (0);
}


/*
 *  XXX: Since we don't have a good way to determine the number of
 *   CPUs allocated to this job on this node, we have to query
 *   the slurm controller (!). 
 *
 *   Hopefully this function can be removed in the near future.
 *   It should only be called when SLURM_JOB_CPUS_PER_NODE is not
 *   set in the environment.
 */
static int query_ncpus_per_node (spank_t sp)
{
    job_info_msg_t * msg;
    uint32_t jobid;
    int cpus_per_node = -1;
    int i;

    if (spank_get_item (sp, S_JOB_ID, &jobid) != ESPANK_SUCCESS) {
        if (verbose)
            fprintf (stderr, "auto-affinity: Failed to get my JOBID!\n");
        return (-1);
    }

    if (slurm_load_jobs (0, &msg, 0) < 0) {
        slurm_error ("auto-affinity: slurm_load_jobs: %m\n");
        return (-1);
    }

    for (i = 0; i < msg->record_count; i++) {
        job_info_t *j = &msg->job_array[i];

        if (j->job_id == jobid) {
            /*
             * XXX: Assume cpus_per_node is the same across the whole job.
             */
            cpus_per_node = (int) j->cpus_per_node[0];
            break;
        }
    }

    slurm_free_job_info_msg (msg);
    return (cpus_per_node);
}


/*
 *  Return 1 if job has allocated all CPUs on this node
 */
static int job_is_exclusive (spank_t sp) 
{
    const char var[] = "SLURM_JOB_CPUS_PER_NODE";
    char val[16];
    int n;

    if (spank_getenv (sp, var, val, sizeof (val)) != ESPANK_SUCCESS) {
        if (verbose)
            fprintf (stderr, "auto-affinity: Failed to find %s in env\n",
                    "SLURM_JOB_CPUS_PER_NODE");

        /* XXX: Now query slurm controller for this information */
        if ((n = query_ncpus_per_node (sp)) < 0) {
            fprintf (stderr, "auto-affinity: Unabled to determine ncpus!\n");
            return (0);
        }
    } 
    else if ((n = str2int (val)) < 0) {
        fprintf (stderr, "auto-affinity: %s=%s invalid\n",
                "SLURM_JOB_CPUS_PER_NODE", val);
        return (0);
    }

    return (n == ncpus);
}


int slurm_spank_init (spank_t sp, int ac, char **av)
{
    if (!spank_remote (sp))
        return (0);

    if (parse_argv (ac, av, spank_remote (sp)) < 0)
        return (-1);

    /*
     *  First get total number of online CPUs
     */
    if ((ncpus = (int) sysconf (_SC_NPROCESSORS_ONLN)) < 0) {
        slurm_error ("Failed to get number of processors: %m\n");
        return (-1);
    }

    if (spank_get_item (sp, S_JOB_LOCAL_TASK_COUNT, &ntasks) != ESPANK_SUCCESS) 
    {
        slurm_error ("Failed to get number of local tasks\n");
        return (-1);
    }

    return (0);
}

/*
 *  Use the slurm_spank_user_init callback to check for exclusivity
 *   becuase user options are processed prior to calling here.
 *   Otherwise, we would not be able to use the `verbose' flag.
 */
int slurm_spank_user_init (spank_t sp, int ac, char **av)
{
    if (!spank_remote (sp))
        return (0);

    if (exclusive_only && !job_is_exclusive (sp)) {
        if (verbose) 
            fprintf (stderr, "auto-affinity: Disabling. "
                    "(job doesn't have exclusive access to this node)\n");
        enabled = 0;
    }

    if (exclusive_only && 
        (ntasks < ncpus_available) && (ncpus_available % ntasks)) {
        if (verbose) 
            fprintf (stderr, "auto-affinity: Disabling. "
                    "ncpus must be evenly divisible by number of tasks\n");
        enabled = 0;
    }

    return (0);
}

static int cpu_set_count (cpu_set_t *setp)
{
    int i;
    int n = 0;
    for (i = 0; i < ncpus; i++) {
        if (CPU_ISSET (i, setp))
            n++;
    }
    return (n);
}

static char * cpuset_to_cstr (cpu_set_t *mask, char *str)
{
    int i;
    char *ptr = str;
    int entry_made = 0;

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, mask)) {
            int j;
            int run = 0;
            entry_made = 1;
            for (j = i + 1; j < CPU_SETSIZE; j++) {
                if (CPU_ISSET(j, mask))
                    run++;
                else
                    break;
            }
            if (!run)
                sprintf(ptr, "%d,", i);
            else if (run == 1) {
                sprintf(ptr, "%d,%d,", i, i + 1);
                i++;
            } else {
                sprintf(ptr, "%d-%d,", i, i + run);
                i += run;
            }
            while (*ptr != 0)
                ptr++;
        }
    }
    ptr -= entry_made;
    *ptr = 0;

    return str;
}

static int get_cpus_per_task ()
{
    if (requested_cpus_per_task)
        return (requested_cpus_per_task);
    else if ((ncpus_available % ntasks) == 0)
        return (ncpus_available / ntasks);
    else
        return (1);
}

/*
 *  Return the absolute cpu number for relative CPU cpu within
 *   the available cpus mask 'cpus_available'.
 */
static int mask_to_available (int cpu)
{
    int i;
    int j = 0;
    for (i = 0; i < ncpus; i++) {
        if (CPU_ISSET (i, &cpus_available) && (cpu == j++))
            return (i);
    }
    slurm_error ("Yikes! Couldn't convert CPU%d to available CPU!", cpu);
    return (-1);
}

static int generate_mask (cpu_set_t *setp, int localid)
{
    int i = 0;
    int cpu;
    int cpus_per_task = get_cpus_per_task ();

    if (cpus_per_task == 1) {
        if ((cpu = mask_to_available (localid + startcpu)) < 0) 
            return (-1);
        CPU_SET (cpu, setp);
        return (0);
    }

    cpu = ((localid * cpus_per_task) + startcpu) % ncpus_available;

    while (i++ < cpus_per_task) {
        int bit = mask_to_available (cpu);
        if (bit < 0) 
            return (-1);
        CPU_SET (bit, setp);
        cpu = (cpu + 1) % ncpus_available;
    }

    return (0);
}

static int generate_mask_reverse (cpu_set_t *setp, int localid)
{
    int i = 0;
    int cpu;
    int cpus_per_task = get_cpus_per_task ();
    int lastcpu = ncpus_available - 1;

    if (cpus_per_task == 1) {
        cpu = (lastcpu - (localid + startcpu) % ncpus_available);
        if ((cpu = mask_to_available (cpu)) < 0) 
            return (-1);
        CPU_SET (cpu, setp);
        return (0);
    }

    cpu = lastcpu - (((localid * cpus_per_task) + startcpu) % ncpus_available);

    while (i++ < cpus_per_task) {
        int bit = mask_to_available (cpu);
        if (bit < 0)
            return (-1);
        CPU_SET (bit, setp);
        cpu = (--cpu >= 0) ? cpu : (ncpus_available - 1);
    }

    return (0);
}

/*
 *  Set the provided cpu set to the actual CPUs available to the
 *   current task (which may be restricted by cpusets or other 
 *   mechanism. 
 * 
 *  Returns the number of cpus set in setp.
 *
 */
static int get_cpus_available (cpu_set_t *setp)
{
    if (sched_getaffinity (0, sizeof (cpu_set_t), setp) < 0) {
        slurm_error ("auto-affinity: sched_getaffinity: %m");
        return (-1);
    }

    return (cpu_set_count (setp));
}

int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
    if (!spank_remote (sp))
        return (0);
    /*
     *  Set available cpus mask after user options have been processed,
     *   in case our cpuset changed.
     */
    ncpus_available = get_cpus_available (&cpus_available);
    return (0);
}

int check_task_cpus_available (void)
{
    int n;

    /*
     *  Check number of available cpus again. If it has
     *   changed since checking in spank_init_post_opt,
     *   then abort, because likely something else is adjusting
     *   the cpu mask (or we are using per-task cpusets)
     *   and auto-affinity is not warranted.
     */
     if ((n = get_cpus_available (&cpus_available)) && 
         (n != ncpus_available) ) {
         if (ncpus_available > 0) {
             if (verbose)
                 fprintf (stderr, "auto-affinity: Not adjusting CPU mask. "
                         "(task cpu mask adjusted externally)\n");
             return (-1);
         }

         ncpus_available = n;
     }

     return (0);
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    int localid;
    cpu_set_t setp[1];
    char buf[4096];

    if (!enabled)
        return (0);

    if (check_task_cpus_available () < 0)
        return (0);

    if (ncpus_available <= 1)
        return (0);

    if ((ntasks <= 1) && !requested_cpus_per_task) {
        if (verbose)
            fprintf (stderr, "auto-affinity: Not adjusting CPU mask. " 
                    "(%d task on this node)\n", ntasks);
        return (0);
    }

    /*
     * Do nothing if user is overcommitting resources
     */
    if (ntasks > ncpus_available)
        return (0);

    /*
     * Do nothing by default if number of CPUs is not a multiple
     *  of the number of tasks
     */
    if ((ncpus_available % ntasks) && !requested_cpus_per_task) {
        if (verbose) {
            fprintf (stderr, "auto-affinity: Not adjusting mask. "
                    "(%d tasks not evenly divided among %d CPUs)\n", 
                    ntasks, ncpus_available);
            fprintf (stderr, "To force, explicity set cpus-per-task\n");
        }
        return (0);
    }

    spank_get_item (sp, S_TASK_ID, &localid);

    if (requested_cpus_per_task > ncpus_available) {
        if (localid == 0)
            slurm_error ("auto-affinity cpus_per_task=%d > ncpus=%d. %s...",
                    requested_cpus_per_task, ncpus_available, "Ignoring");
        requested_cpus_per_task = 0;
    }

    CPU_ZERO (setp);

    if (reverse)
        generate_mask_reverse (setp, localid);
    else
        generate_mask (setp, localid);

    if (verbose)
        fprintf (stderr, "%s: local task %d: CPUs: %s\n", 
                "auto-affinity", localid, cpuset_to_cstr (setp, buf));

    if (sched_setaffinity (getpid (), sizeof (*setp), setp) < 0) {
        slurm_error ("Failed to set auto-affinity for task %d: %s\n",
                localid, strerror (errno));
        return (-1);
    }

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
