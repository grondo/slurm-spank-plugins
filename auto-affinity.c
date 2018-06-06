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
#include <ctype.h>

#define __USE_GNU
#include <sched.h>

/* Include list.h before slurm.h since slurm header stole our List type */
#include "lib/list.h"

#include <slurm/slurm.h>
#include <slurm/spank.h>

#include "lib/split.h"
#include "lib/fd.h"

SPANK_PLUGIN(auto-affinity, 1);

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
  cpt=N             Shorthand for cpus_per_task.\n\
\n\
The following options may be used to explicitly list the CPUs for each\n\
task on a node.\n\
  cpus=LIST         Comma-separated list of CPUs to allocate to tasks\n\
  masks=LIST        Comma-separated mask of CPUs to allocate to each task\n\
\n\
Where the cpu and mask lists are of the same format documented in the\n\
cpuset(4) manpage in the FORMATS section. Masks may be optionally followed\n\
by a repeat count (e.g. 0xf0*2 == 0xf0,0xf0)\n\
\n\
If one of the cpus= or masks= options is used, it must be the last option\n\
specified, and any 'reverse' or 'start' option will be ignored\n\
\n\n";


/*****************************************************************************
 *
 *  Global auto-affinity variables
 *
 ****************************************************************************/

static int ncpus  = -1;
static int ntasks = -1;
static int enabled = 1;    /* True if enabled by configuration              */
static int disabled = 0;   /* True if disabled by --auto-affinity=off       */
static int verbose = 0;
static int reverse = 0;
static int startcpu = 0;
static int requested_cpus_per_task = 0;

/*
 *  Variables for explicit user CPU/core mapping.
 */
static int        nlist_elements = 0;    /* Number of elements in following  */
static char       *cpus_list = NULL;     /* cstr-style list of CPUs          */
static cpu_set_t *cpu_mask_list = NULL;  /* array of CPU masks               */

static int exclusive_only = 0; /*  Only set affinity if this job has         *
                                *   exclusive access to this node            */
static int multiples_only = 0; /*  Only set affinity if ncpus is a multiple  *
                                *   of ntasks                                */

/*
 *  CPU position map (logical to physical CPU/core mapping)
 */
static cpu_set_t cpus_available;
static int       ncpus_available;
static int      *cpu_position_map = NULL;

/*****************************************************************************
 *
 *  Forward declarations
 *
 ****************************************************************************/

static int parse_user_option (int val, const char *optarg, int remote);
static char * cpuset_to_cstr (cpu_set_t *mask, char *str);
static int cstr_count (const char *str);
static int cstr_to_cpu_id (const char *str, int n);
static int str_to_cpuset(cpu_set_t *mask, const char* str);
static int cpu_set_count (cpu_set_t *setp);


/*****************************************************************************
 *
 *  SPANK plugin options:
 *
 ****************************************************************************/

struct spank_option spank_options [] = {
    { "auto-affinity", "[args]",
      "Automatic, best guess CPU affinity for SMP machines " 
          "(args=`help' for more info)",
      2, 0, (spank_opt_cb_f) parse_user_option 
    },
    SPANK_OPTIONS_TABLE_END
};


/*****************************************************************************
 *
 *  Functions:
 *
 ****************************************************************************/

static int str2int (const char *str)
{
    char *p;
    long l = strtol (str, &p, 10);

    if (p && (*p != '\0'))
        return (-1);

    return ((int) l);
}

static int parse_option (const char *opt, int remote)
{
    if (strcmp (opt, "off") == 0)
        disabled = 1;
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
    else if ((strcmp (opt, "help") == 0) && !remote) {
        fprintf (stderr, auto_affinity_help);
        exit (0);
    }

    return (0);

    fail:
    slurm_error ("auto-affinity: Invalid option: `%s'", opt);
    return (-1);
}

static void list_clear (List l)
{
    char *s;
    while ((s = list_pop (l)))
        free (s);
}


static int parse_cpus_list (List l)
{
    char str [4096];

    /*  Recreate cpus= argument
     */
    list_join (str, sizeof (str), ",", l);

    if ((nlist_elements = cstr_count (str)) < 0) {
        fprintf (stderr, "auto-affinity: Failed to parse '%s'\n", str);
        return (-1);
    }

    if (nlist_elements == 0) {
        fprintf (stderr, "auto-affinity: No cpus in '%s'\n", str);
        return (-1);
    }

    cpus_list = strdup (str);

    list_clear (l);

    return (0);
}

static List cpu_mask_list_expand (List l)
{
    int i, n;
    char *s;
    List mask_list;
    mask_list = list_create ((ListDelF) free);

    while ((s = list_pop (l))) {
        char *rep = strchr (s, '*');
        if (rep) {
            *rep = '\0';
            n = atoi (rep+1);
            for (i = 0; i < n; i++)
                list_append (mask_list, strdup (s));
        }
        else
            list_append (mask_list, strdup (s));

        free (s);
    }
    return (mask_list);
}


static int parse_cpu_mask_list (List l)
{
    char *s;
    int n;
    int i = 0;
    int rc = 0;

    List mask_list = cpu_mask_list_expand (l);

    nlist_elements = n = list_count (mask_list);
    cpu_mask_list = malloc (n * sizeof(cpu_set_t));

    while ((s = list_pop (mask_list))) {
        if ((rc = str_to_cpuset (&cpu_mask_list[i++], s)) < 0)
            fprintf (stderr, "auto-affinity: Invalide cpu mask '%s'\n", s);
        free (s);
    }
    return rc;
}


static int parse_user_option (int val, const char *arg, int remote)
{
    char *str;
    List l;
    int rc = 1;

    if (arg == NULL)
        return (0);

    str = strdup (arg);
    l = list_split (",", str);
    free (str);

    while ((str = list_pop (l))) {
        /*
         *  For cpus= and masks=, the rest of the line
         *   is taken to be part of the option:
         */
        if (strncmp (str, "cpus=", 5) == 0) {
            list_push (l, strdup (str+5));
            rc = parse_cpus_list (l);
        }
        else if (strncmp (str, "masks=", 6) == 0) {
            list_push (l, strdup (str+6));
            rc = parse_cpu_mask_list (l);
        }
        else
            rc = parse_option (str, remote);
        free (str);
    }

    list_destroy (l);

    return (rc);
}

static int parse_argv (int ac, char **av, int remote)
{
    int i;
    for (i = 0; i < ac; i++) {
        if (strcmp (av[i], "off") == 0)
            disabled = 1;
        else if (strcmp (av[i], "exclusive_only") == 0)
            exclusive_only = 1;
        else if (strcmp (av[i], "multiples_only") == 0)
            multiples_only = 1;
        else
            return (-1);
    }
    return (0);
}

static int read_file_int (const char *path)
{
    int val;
    FILE *fp = fopen (path, "r");

    if (fp == NULL)
        return (-1);

    if (fscanf (fp, "%d", &val) != 1) {
        slurm_error ("auto-affinity: failed to read %s: %m\n", path);
        val = -1;
    }

    fclose (fp);

    return (val);
}

/*****************************************************************************
 *
 *  CPU position map functions:
 *
 ****************************************************************************/

struct cpu_info {
    int id;
    int pkgid;
    int coreid;
};

static int lookup_cpu_info (struct cpu_info *cpu)
{
    const char cpudir[] = "/sys/devices/system/cpu";
    char path [4096];

    snprintf (path, sizeof (path), 
            "%s/cpu%d/topology/physical_package_id", cpudir, cpu->id);

    cpu->pkgid = read_file_int (path);
    if (cpu->pkgid < 0)
        return (-1);

    snprintf (path, sizeof (path), 
            "%s/cpu%d/topology/core_id", cpudir, cpu->id);

    cpu->coreid = read_file_int (path);
    if (cpu->coreid < 0)
        return (-1);

    return (0);
}


static void cpu_info_destroy (struct cpu_info *cpu)
{
    if (cpu)
        free (cpu);
}

static struct cpu_info * cpu_info_create (int id)
{
    struct cpu_info *cpu = malloc (sizeof (struct cpu_info));

    if (!cpu)
        return NULL;

    cpu->id = id;

    if (lookup_cpu_info (cpu) < 0) {
        slurm_error ("auto-affinity: Failed to get info for cpu%d\n", id);
        cpu_info_destroy (cpu);
        return (NULL);
    }
        
    return (cpu);
}

static int cpu_info_cmp (struct cpu_info *cpu1, struct cpu_info *cpu2)
{
    if (cpu1->pkgid == cpu2->pkgid)
        return (cpu1->coreid - cpu2->coreid);
    else 
        return (cpu1->pkgid - cpu2->pkgid);
}

static int create_cpu_position_map (int ncpus)
{
    List cpu_info_list;
    ListIterator iter;
    int i;
    struct cpu_info *cpu;

    cpu_info_list = list_create ((ListDelF) cpu_info_destroy);

    for (i = 0; i < ncpus; i++) {
        if ((cpu = cpu_info_create (i)) == NULL)
            return (-1);
        list_push (cpu_info_list, cpu);
    }

    /*
     *  Sort list of CPUs by physical location.
     */
    list_sort (cpu_info_list, (ListCmpF) cpu_info_cmp);

    /*
     *  Build array to map cpu position back to cpu logical id:
     */
    i = 0;
    cpu_position_map = malloc (ncpus * sizeof (int));

    iter = list_iterator_create (cpu_info_list);

    while ((cpu = list_next (iter)))
        cpu_position_map[i++] = cpu->id;

    list_destroy (cpu_info_list);

    return (0);
}

static int cpu_position_to_id (int n)
{
    return cpu_position_map [n];
}

static int get_nodeid (spank_t sp)
{
    int nodeid = -1;
    if (spank_get_item (sp, S_JOB_NODEID, &nodeid) != ESPANK_SUCCESS) {
        if (verbose)
            fprintf (stderr, "auto-affinity: Failed to get my NODEID!\n");
        return (-1);
    }
    return (nodeid);
}

/*****************************************************************************
 *
 *  Utility functions
 *
 ****************************************************************************/

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
            job_resources_t *jres = j->job_resrcs;
            int nodeid = get_nodeid (sp);

            if (nodeid < 0)
                break;

            cpus_per_node = slurm_job_cpus_allocated_on_node_id (jres, nodeid);
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

/*
 *  Return 1 if this step is a batch script
 */
static int job_step_is_batch (spank_t sp)
{
    uint32_t stepid;

    if (spank_get_item (sp, S_JOB_STEPID, &stepid) != ESPANK_SUCCESS) {
        slurm_error ("auto-affinity: Failed to get job stepid!");
        return (0);
    }

    if (stepid == 0xfffffffe)
        return (1);
    return (0);
}


/*****************************************************************************
 *
 *  SPANK callback functions:
 *
 ****************************************************************************/


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

    if (create_cpu_position_map (ncpus) < 0)
        return (-1);

    if (spank_get_item (sp, S_JOB_LOCAL_TASK_COUNT, &ntasks) != ESPANK_SUCCESS) 
    {
        slurm_error ("Failed to get number of local tasks\n");
        return (-1);
    }

    return (0);
}

int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    if (!spank_remote (sp))
        return (0);

    if (cpu_position_map != NULL)
        free (cpu_position_map);

    if (cpus_list != NULL)
        free (cpus_list);

    if (cpu_mask_list != NULL)
        free (cpu_mask_list);

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

    /*
     *  In some versions of SLURM, batch script job steps appear as
     *   if the user explicitly set --cpus-per-task, and this may
     *   cause unexpected behavior. It is much safer to just disable
     *   auto-affinity behavior for batch scripts.
     */
    if (job_step_is_batch (sp))
        return (0);

    /*  Enable CPU affinity operation only if we make it through
     *   the following checks.
     */
    enabled = 0;

    if (exclusive_only && !job_is_exclusive (sp)) {
        if (verbose) 
            fprintf (stderr, "auto-affinity: Disabling. "
                    "(job doesn't have exclusive access to this node)\n");
        return (0);
    }

    /*
     *  Set requested_cpus_per_task to the number of cpus_per_task
     *    recorded by slurm, but only if that value was set
     *    explicitly by the user (i.e. the user ran with -c 2 or greater)
     */
    if (!requested_cpus_per_task) {
        int cpt;
        int rc = spank_get_item (sp, S_STEP_CPUS_PER_TASK, &cpt);
        if ((rc == ESPANK_SUCCESS) && (cpt > 1))
            requested_cpus_per_task = cpt;
    }

    /*  Enable by default if user requested cpt, cpus= or masks=.
     */
    if (requested_cpus_per_task || cpus_list || cpu_mask_list) {
        enabled = 1;
        return (0);
    }

    /*
     *   Don't do anything for overcommit
     */
    if (ntasks > ncpus_available) {
        if (verbose)
            fprintf (stderr, "auto-affinity: Disabling due to overcommit.\n");
        return (0);
    }

    /*
     *  If ncpus is a multiple of ntasks then enable auto-affinity.
     */
    if ((ncpus_available % ntasks) == 0) {
        enabled = 1;
        return (0);
    }

    /*
     *  If multiples_only is set or ncpus/ntasks == 1,
     *   then do nothing.
     */
    if (multiples_only || (ncpus_available/ntasks == 1)) {
        if (verbose) {
            fprintf (stderr, "auto-affinity: Not adjusting mask. "
                    "(%d tasks not evenly divided among %d CPUs)\n", 
                    ntasks, ncpus_available);
            fprintf (stderr, "To force, explicity set cpus-per-task\n");
        }
        return (0);
    }

    /*
     *  Now we know ncpus/ntasks > 1.
     *   Set this as the requested CPUs/task  and enable auto-affinity:
     */
    requested_cpus_per_task = ncpus_available/ntasks;
    enabled = 1;

    return (0);
}

static int cpu_set_count (cpu_set_t *setp)
{
    int i;
    int n = 0;
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, setp))
            n++;
    }
    return (n);
}

static int get_cpus_per_task ()
{
    if (requested_cpus_per_task)
        return (requested_cpus_per_task);
    else
        return (ncpus_available / ntasks);
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

static int cpu_set_physical_to_logical (cpu_set_t *setp)
{
    int i;
    cpu_set_t lcpus = *setp;

    CPU_ZERO (setp);

    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET (i, &lcpus)) {
            int cpu = cpu_position_to_id (i);
            CPU_SET (mask_to_available (cpu), setp);
        }
    }

    return (0);
}

static int generate_mask (cpu_set_t *setp, int localid)
{
    int i = 0;
    int cpu;
    int cpus_per_task = get_cpus_per_task ();

    if (cpus_per_task == 1) {
        int n = cpu_position_to_id (localid + startcpu);
        if ((cpu = mask_to_available (n)) < 0) 
            return (-1);
        CPU_SET (cpu, setp);
        return (0);
    }

    cpu = ((localid * cpus_per_task) + startcpu) % ncpus_available;

    while (i++ < cpus_per_task) {
        int bit = mask_to_available (cpu_position_to_id (cpu));
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
        if ((cpu = mask_to_available (cpu_position_to_id (cpu))) < 0) 
            return (-1);
        CPU_SET (cpu, setp);
        return (0);
    }

    cpu = lastcpu - (((localid * cpus_per_task) + startcpu) % ncpus_available);

    while (i++ < cpus_per_task) {
        int bit = mask_to_available (cpu_position_to_id (cpu));
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

static void generate_mask_from_cpus_list (const char *cpus_list,
        cpu_set_t *setp, int localid)
{
    if (requested_cpus_per_task) {
        int i;
        int idx = localid * requested_cpus_per_task;
        for (i = 0; i < requested_cpus_per_task; idx++, i++)
            CPU_SET (cstr_to_cpu_id (cpus_list, idx % nlist_elements), setp);
    }
    else
        CPU_SET (cstr_to_cpu_id (cpus_list, localid % nlist_elements), setp);
}


int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    int localid;
    cpu_set_t setp[1];
    char buf[4096];

    if (!enabled || disabled)
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

    spank_get_item (sp, S_TASK_ID, &localid);

    if (requested_cpus_per_task > ncpus_available) {
        if (localid == 0)
            slurm_error ("auto-affinity cpus_per_task=%d > ncpus=%d. %s...",
                    requested_cpus_per_task, ncpus_available, "Ignoring");
        requested_cpus_per_task = 0;
    }

    CPU_ZERO (setp);

    if (cpus_list) {
        generate_mask_from_cpus_list (cpus_list, setp, localid);
        cpu_set_physical_to_logical (setp);
    }
    else if (cpu_mask_list) {
        *setp = cpu_mask_list[localid % nlist_elements];
        cpu_set_physical_to_logical (setp);
    }
    else if (reverse)
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

/*****************************************************************************
 *
 *  cpu_set_t parsing functions
 *
 *  The following code taken from taskset.c in util-linux
 *
 *  Copyright (C) 2004 Robert Love
 *
 ****************************************************************************/

static inline int char_to_val(int c)
{
    int cl;

    cl = tolower(c);
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (cl >= 'a' && cl <= 'f')
        return cl + (10 - 'a');
    else
        return -1;
}

static int str_to_cpuset(cpu_set_t *mask, const char* str)
{
    int len = strlen(str);
    const char *ptr = str + len - 1;
    int base = 0;

    /* skip 0x, it's all hex anyway */
    if (len > 1 && !memcmp(str, "0x", 2L))
        str += 2;

    CPU_ZERO(mask);
    while (ptr >= str) {
        char val = char_to_val(*ptr);
        if (val == (char) -1)
            return -1;
        if (val & 1)
            CPU_SET(base, mask);
        if (val & 2)
            CPU_SET(base + 1, mask);
        if (val & 4)
            CPU_SET(base + 2, mask);
        if (val & 8)
            CPU_SET(base + 3, mask);
        len--;
        ptr--;
        base += 4;
    }

    return 0;
}

static const char * nexttoken (const char *p, int sep)
{
    if (p)
        p = strchr (p, sep);
    if (p)
        p++;
    return (p);
}

/*
 *  Modified version of cstr_to_cpuset to count CPUs in cstr.
 */
static int cstr_count (const char *str)
{
    int count = 0;
    const char *p, *q;
    q = str;

    while (p = q, q = nexttoken(q, ','), p) {
        unsigned int a; /* beginning of range */
        unsigned int b; /* end of range */
        unsigned int s; /* stride */
        const char *c1, *c2;

        if (sscanf(p, "%u", &a) < 1)
            return 1;
        b = a;
        s = 1;

        c1 = nexttoken(p, '-');
        c2 = nexttoken(p, ',');
        if (c1 != NULL && (c2 == NULL || c1 < c2)) {
            if (sscanf(c1, "%u", &b) < 1)
                return -1;
            c1 = nexttoken(c1, ':');
            if (c1 != NULL && (c2 == NULL || c1 < c2))
                if (sscanf(c1, "%u", &s) < 1) {
                    return -1;
                }
        }

        if (!(a <= b))
            return 1;
        while (a <= b) {
            count++;
            a += s;
        }
    }

    return count;

}

/*
 *   Modified version of cstr_to_cpuset to return the cpu
 *    at index n in the cstr.
 */
static int cstr_to_cpu_id (const char *str, int n)
{
    int index = 0;
    const char *p, *q;
    q = str;

    while (p = q, q = nexttoken(q, ','), p) {
        unsigned int a; /* beginning of range */
        unsigned int b; /* end of range */
        unsigned int s; /* stride */
        const char *c1, *c2;

        if (sscanf(p, "%u", &a) < 1)
            return 1;
        b = a;
        s = 1;

        c1 = nexttoken(p, '-');
        c2 = nexttoken(p, ',');
        if (c1 != NULL && (c2 == NULL || c1 < c2)) {
            if (sscanf(c1, "%u", &b) < 1)
                return -11;
            c1 = nexttoken(c1, ':');
            if (c1 != NULL && (c2 == NULL || c1 < c2))
                if (sscanf(c1, "%u", &s) < 1) {
                    return -11;
                }
        }

        if (!(a <= b))
            return 1;
        while (a <= b) {
            if (index == n)
                return (a);
            a += s;
            index++;
        }
    }

    return -1;
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



/*
 * vi: ts=4 sw=4 expandtab
 */
