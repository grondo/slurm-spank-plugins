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


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <slurm/spank.h>

/* SGI libcpuset */
#include <bitmask.h>
#include <cpuset.h>

#include "fd.h"
#include "list.h"
#include "split.h"
#include "util.h"
#include "create.h"
#include "conf.h"
#include "log.h"
#include "slurm.h"

SPANK_PLUGIN (cpuset, 1)

/*
 *  Help message for user option
 */
static const char cpuset_help_string [] =
"\
use-cpusets: Automatically allocate cpusets to each step within a job.\n\
\n\
When using the SLURM cpuset.so plugin, the default behavior is to allocate\n\
one cpuset per job, and run all subsequent job steps within the job cpuset.\n\
When using --use-cpusets, the cpuset plugin will re-allocate CPUs and\n\
optionally memory nodes from the job cpuset into a child cpuset for the\n\
executing job step. This allows convenient separation of multiple job steps \n\
being run in parallel within a single job allocation.\n\
\n\
By default, the same allocation options are used for job steps as are\n\
configured for jobs. These options can be tuned by providing arguments to\n\
the --use-cpusets option.\n\
\n\
Option Usage: --use-cpusets=[args...]\n\
\n\
where args... is a comma separated list of one or more of the following\n\
  help                 Display this message.\n\
  debug                Enable verbose debugging messages.\n\
  tasks                Additionally constrain tasks to cpusets.\n\
\n\
 Policy options:\n\
  best-fit             Allocate tasks to most full nodes/sockets first.\n\
  worst-fit            Allocate tasks to least full nodes/sockets first.\n\
  first-fit            Allocate tasks to first free slots found.\n\
  reverse              Reverse CPU allocation order (start at last CPU).\n\
  order=normal         Normal CPU allocation order (start at first CPU).\n\
  no-idle              Do not try to allocate whole idle nodes first.\n\
\n\
  idle-first=[policy]  Use [policy] to allocate idle nodes first, where\n\
                       policy is one of:\n\
                        gt    Allocate idle nodes first if the number of \n\
                               tasks is greater than or equal to the size \n\
                               of a socket/NUMA node.\n\
                        mult  Allocate idle nodes first only if the number of\n\
                               tasks in the job step is a multiple of the\n\
                               size of a socket/NUMA node.\n\
                        no    Equivalent to no-idle.\n\
\n\
   nomem               Do not also constrain memory to the local nodes of\n\
                        the selected CPUs.\n\n";

static List user_options = NULL;

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static cpuset_conf_t conf = NULL;

//static int step_cpuset_created = 0;  
static int per_task_cpuset = 0;      /* --use-cpuset=tasks */

static uint32_t jobid;
static uint32_t stepid;
static int step_ncpus = -1;
static int ncpus_per_task = -1;
static int debug_level = 0;
static int user_debug_level = 0;

static int parse_one_option (const char *opt)
{
    if (strncmp ("debug=", opt, 6) == 0) 
        debug_level = str2int (opt + 6);
    else if (strcmp ("debug", opt) == 0) 
        debug_level = 1;
    else
        return (cpuset_conf_parse_opt (conf, opt));

    return (0);
}

static int parse_options (int ac, char **av)
{
    int i;
    for (i = 0; i < ac; i++) 
        parse_one_option (av[i]);
    return (0);
}

static int get_nodeid (spank_t sp)
{
    int nodeid = -1;
    if (spank_get_item (sp, S_JOB_NODEID, &nodeid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get my nodeid\n");
        return (-1);
    }
    return (nodeid);
}

/*
 *  XXX: Since we don't have a good way to determine the number of
 *   CPUs allocated to this job on this node, we have to query
 *   the slurm controller (!). 
 *
 */
static int query_ncpus_per_node (spank_t sp, uint32_t jobid)
{
    job_info_msg_t * msg;
    uint16_t cpus_per_node = 0;
    int i;

    /*
     *  Use the S_JOB_NCPUS spank item if it exists:
     */
    if (spank_get_item (sp, S_JOB_NCPUS, &cpus_per_node) == ESPANK_SUCCESS) {
        cpuset_debug ("S_JOB_NCPUS = %d\n", cpus_per_node);
        return (cpus_per_node);
    }

    /*
     * Otherwise, we have to query all jobs and find the right job record.
     */
    /*
     *  Ensure that libslurm.so has been dlopened with RTLD_GLOBAL passed.
     */
    dyn_slurm_open ();

    if (slurm_load_job (&msg, jobid, SHOW_DETAIL) < 0) {
        cpuset_error ("slurm_load_job: %s\n", slurm_strerror (errno));
        return (-1);
    }

    for (i = 0; i < msg->record_count; i++) {
        job_info_t *j = &msg->job_array[i];

        if (j->job_id == jobid) {
            int nodeid;
            job_resources_t *jres = j->job_resrcs;
            if ((nodeid = get_nodeid (sp)) >= 0)
                cpus_per_node =
                    slurm_job_cpus_allocated_on_node_id (jres, nodeid);
            break;
        }
    }

    slurm_free_job_info_msg (msg);
    if (cpus_per_node == 0)
        cpuset_error ("Failed to get nCPUs for this node: %s\n", slurm_strerror (errno));
    return (cpus_per_node);
}

int migrate_job_to_cpuset (uint32_t jobid, uid_t uid, pid_t pid)
{
    int rc;
    char path[4096];
    int n = 0;

    cpuset_getcpusetpath (0, path, sizeof (path));

    if (pid)
        cpuset_debug ("Migrate: Moving %d from cpuset %s\n", pid, path);
    else
        cpuset_debug ("Migrate: Moving from cpuset %s\n", path);
    /*
     *  If we're not under /slurm, prepend user cpuset
     */
    if (strncmp (path, "/slurm", 6) != 0) 
        n = snprintf (path, sizeof (path), "/slurm/%d", uid);
    else
        n = strlen (path);

    /*
     *  Now everything happens relative to current cpuset
     */
    rc = snprintf (path + n, sizeof (path) - n, "/%u", jobid);

    if (rc < 0 || rc > sizeof (path)) {
        cpuset_error ("job%u: snprintf failed: %s\n", jobid, strerror (errno));
        return (-1);
    }

    if (pid)
        cpuset_debug ("Migrate: Moving %d to cpuset %s\n", pid, path);
    else
        cpuset_debug ("Migrate: Moving to cpuset %s\n", path);

    if (cpuset_move (pid, path) < 0) 
        return (-1);
    return (0);
}

static int job_ncpus_per_task (spank_t sp)
{
    const char var[] = "SLURM_CPUS_PER_TASK";
    char val [128];

    if (ncpus_per_task < 0) {
        if (spank_getenv (sp, var, val, sizeof (val)) != ESPANK_SUCCESS) {
            //cpuset_error ("getenv (SLURM_CPUS_PER_TASK) failed\n");
            return (-1);
        }
        ncpus_per_task = str2int(val);
    }
    return (ncpus_per_task);
}

static int job_step_ncpus (spank_t sp)
{
    uint32_t ntasks;

    if (spank_get_item (sp, S_JOB_LOCAL_TASK_COUNT, &ntasks) != ESPANK_SUCCESS)
        return (-1);

    return (job_ncpus_per_task (sp) * ntasks);
}

static int log_slurm  (const char *msg) 
{ 
    slurm_info ("%s", msg); 
    return (0); 
}

static int log_stderr (const char *msg) 
{ 
    fprintf (stderr, "%s", msg); 
    return (0); 
}


static int migrate_to_user_cpuset  (uid_t uid)
{
    int rc;
    char path [128];

    rc = snprintf (path, sizeof (path), "/slurm/%d", uid);
    if (rc < 0 || rc > sizeof (path))
        return (-1);

    if (cpuset_move (0, path) < 0)
        return (-1);

    return (0);
}


int slurm_spank_task_exit (spank_t sp, int ac, char *av[])
{
    uid_t uid;
    static uint32_t n = 0;
    static int nexited = 0;

    if (n == 0 &&
        spank_get_item (sp, S_JOB_LOCAL_TASK_COUNT, &n) != ESPANK_SUCCESS) {
        cpuset_error ("task_exit: Failed to get ntasks\n");
        return (0);
    }

    if (++nexited < n)
        return (0);

    /*
     *  There are no more user tasks. Move this slurm daemon
     *   out of the job/job step cpuset back to the user cpuset
     *   in case the daemon hangs around after the job has been
     *   marked as complete by SLURM.
     */
    if (spank_get_item (sp, S_JOB_UID, &uid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get uid: %m\n", strerror (errno));
        return (-1);
    }

    if (migrate_to_user_cpuset (uid) < 0) {
        cpuset_error ("migrate_to_user_cpuset(%d) failed: %s\n",
                uid, strerror (errno));
        /*
         *  Don't make failure here a fatal error. 99% of the time
         *   it is ok since we'll shortly exit.
         */
        return (0);
    }

    return (0);
}

int slurm_spank_init (spank_t sp, int ac, char *av[])
{
    int rc;
    int lockfd;
    uid_t uid;

    if (!spank_remote (sp))
        return (0);

    log_add_dest (1, log_slurm);

    conf = cpuset_conf_create ();
    cpuset_conf_parse_system (conf);

    parse_options (ac, av);

    log_update (debug_level, log_slurm);

    /*
     *  Get jobid
     */
    if (spank_get_item (sp, S_JOB_ID, &jobid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get jobid: %s\n", strerror (errno));
        return (-1);
    }

    if (spank_get_item (sp, S_JOB_STEPID, &stepid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get stepid: %s\n", strerror (errno));
        return (-1);
    }

    if (spank_get_item (sp, S_JOB_UID, &uid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get uid: %m\n", strerror (errno));
        return (-1);
    }

    cpuset_debug ("Attempting to create slurm cpuset\n");
    /*
     *  Try to migrate to existing cpuset for this job. If 
     *   successful, then we're done.
     */
    if ((lockfd = slurm_cpuset_create (conf)) < 0) {
        cpuset_error ("Failed to create/lock slurm cpuset: %s\n", 
                strerror (errno));
        return (-1);
    }

    if ((rc = migrate_job_to_cpuset (jobid, uid, 0)) != 0) {
        /*
         *  No existing job cpuset on this node, create one:
         */
        int ncpus = query_ncpus_per_node (sp, jobid);

        cpuset_debug ("Creating cpuset for job=%d uid=%d ncpus=%d\n",
                jobid, uid, ncpus);

        if ((rc = create_cpuset_for_job (conf, jobid, uid, ncpus)) < 0)
            goto done;

        if ((rc = migrate_job_to_cpuset (jobid, uid, 0)) < 0) {
            log_err ("Failed to migrate jobid %d to cpuset: %s\n",
                    jobid, strerror (errno));
            goto done;
        }
    }

    step_ncpus = job_step_ncpus (sp);

done:
    slurm_cpuset_unlock (lockfd);
    return (rc);
}

/*
 *   User optional per-step cpuset option parsing
 *    Options are processed *after* slurm_spank_init completes,
 *    so we have to create the step cpuset within the option 
 *    handler.
 */

static int set_user_options (int remote)
{
    char *opt;
    ListIterator i;
    int rc = 0;

    if (user_options == NULL)
        return (0);

    i = list_iterator_create (user_options);
    while ((opt = list_next (i))) {
        if (!remote && (strcmp (opt, "help") == 0)) {
            fprintf (stderr, cpuset_help_string);
            exit (0);
        }
        else if (strcmp (opt, "tasks") == 0) 
            per_task_cpuset = 1;
        else if (strncmp ("debug=", opt, 6) == 0) 
            user_debug_level = str2int (opt + 6);
        else if (strcmp ("debug", opt) == 0) 
            user_debug_level = 1;
        else if (parse_one_option (opt) < 0)
            rc = -1;
    }
    /*
     *  Done with user_options now.
     */
    list_destroy (user_options);
    return (rc);
}

static int parse_user_option (int val, const char *optarg, int remote)
{
    int rc = 1;

    log_add_dest (0, log_stderr);

    if (optarg) {
        char *str;
        str = strdup (optarg);
        user_options = list_split (",", str);
        free (str);

        /*
         *  If running 'local' (i.e. in srun), then we may
         *   not yet have created a cpuset configuration object.
         *   We'll need this to test-parse options, so create it now.
         */
        if (!conf) conf = cpuset_conf_create ();

        if (set_user_options (remote) < 0)
            return (-1);

    }

    log_update (user_debug_level, log_stderr);

    if (remote && !spank_symbol_supported ("slurm_spank_init_post_opt")) {
        /*
         *  Must create job step cpuset in option handler unless
         *   init_post_opt callback exists in this version of SLURM.
         */
        int lockfd = slurm_cpuset_lock ();
        if (debug_level > 0 || user_debug_level > 0)
            print_current_cpuset_info ();
        if ((rc = create_cpuset_for_step (conf, stepid, step_ncpus)) < 0) {
            /* 
             *  If step cpuset creation failed, ensure we don't try
             *   to create per-task cpuset.
             */
            cpuset_error ("Failed to create cpuset for step %d: %s\n", 
                    stepid, strerror (errno));
            per_task_cpuset = 0;
        }
        else 
            rc = migrate_job_to_cpuset (stepid, -1, 0);
        slurm_cpuset_unlock (lockfd);
    }

    return (rc);
}

struct spank_option spank_options [] = {
    { "use-cpusets", "[args..]",
      "Use per-job-step and per-task cpusets. (args=`help' for more info)",
      2, 0, (spank_opt_cb_f) parse_user_option
    },
    SPANK_OPTIONS_TABLE_END
};

int slurm_spank_init_post_opt (spank_t sp, int ac, char **av)
{
    int lockfd;
    int rc;

    if (!spank_remote (sp) || !user_options) 
        return (0);

    if ((lockfd = slurm_cpuset_lock ()) < 0)
        return (-1);

    if (debug_level > 0 || user_debug_level > 0)
        print_current_cpuset_info ();

    if ((rc = create_cpuset_for_step (conf, stepid, step_ncpus)) < 0)
        per_task_cpuset = 0;
    else
        rc = migrate_job_to_cpuset (stepid, -1, 0);

    if (debug_level > 0)
        print_current_cpuset_info ();

    slurm_cpuset_unlock (lockfd);

    return (rc);
}

int slurm_spank_task_post_fork (spank_t sp, int ac, char **av)
{
    pid_t task_pid;
    int taskid;
    int lockfd;
    int cpus_per_task;
    int rc;

    if (!per_task_cpuset)
        return (0);

    if (spank_get_item (sp, S_TASK_ID, &taskid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get taskid\n");
        return (-1);
    }

    if (spank_get_item (sp, S_TASK_PID, &task_pid) != ESPANK_SUCCESS) {
        cpuset_error ("Failed to get task pid\n");
        return (-1);
    }

    if ((lockfd = slurm_cpuset_lock ()) < 0)
        return (-1);

    cpus_per_task = job_ncpus_per_task (sp);

    if ((rc = create_cpuset_for_task (conf, taskid, cpus_per_task)) == 0)
        rc = migrate_job_to_cpuset (taskid, -1, task_pid);

    slurm_cpuset_unlock (lockfd);

    return (rc);

}

/*
 * vi: ts=4 sw=4 expandtab
 */
