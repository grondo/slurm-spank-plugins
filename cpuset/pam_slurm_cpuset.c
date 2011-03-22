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



#include <pwd.h>
#include <string.h>
#include <bitmask.h>
#include <cpuset.h>

#define PAM_SM_ACCOUNT
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <security/_pam_macros.h>

#include "create.h"
#include "util.h"
#include "hostlist.h"
#include "slurm.h"
#include "conf.h"
#include "log.h"

static int create_all_job_cpusets (cpuset_conf_t conf, uid_t uid);
static int migrate_to_user_cpuset (uid_t uid);
static int in_user_cpuset (uid_t uid);

static pam_handle_t *pam_handle = NULL;

static const char msg_prefix [] = "";
static const char msg_suffix [] = "\r";

static int debuglevel = 1;


static int log_pam_syslog (const char *msg) {
    pam_syslog (pam_handle, 0, "%s", msg);
    return (0);
}

static int log_pam_error (const char *msg) {
    pam_error (pam_handle, "%s%s%s", msg_prefix, msg, msg_suffix);
    return (0);
}

static int parse_options (cpuset_conf_t conf, int ac, const char **av)
{
    int i;
    for (i = 0; i < ac; i++) {
        if (strcmp ("debug", av[i]) == 0)
            debuglevel++;
        else if (strncmp ("debug=", av[i], 6) == 0)
            debuglevel = 1 + str2int (av[i] + 6);
        else if (cpuset_conf_parse_opt (conf, av[i]) < 0)
            return (-1);
    }
    return (0);
}

PAM_EXTERN int 
pam_sm_acct_mgmt (pam_handle_t *pamh, int flags, int ac, const char **av)
{
    int rc;
    int n;
	const char *user;
    struct passwd *pw;
    uid_t uid;
    const void **uptr = (const void **) &user;
    int lockfd;

    cpuset_conf_t conf = cpuset_conf_create ();

    pam_handle = pamh;

    log_add_dest (debuglevel, log_pam_syslog);
    log_add_dest (0, log_pam_error);
    log_set_prefix ("");

    if ((rc = pam_get_item (pamh, PAM_USER, uptr)) != PAM_SUCCESS
       || user == NULL 
       || *user == '\0') {
        log_err ("get PAM_USER: %s", pam_strerror (pamh, rc));
        return (PAM_USER_UNKNOWN);
    }

    if (!(pw = getpwnam (user))) {
        log_err ("User (%s) does not exist.", user);
        return (PAM_USER_UNKNOWN);
    }

    uid = pw->pw_uid; 

    if (uid == 0)
        return (PAM_SUCCESS);

    /*
     *  If we're already in the user's cpuset, bail early
     */
    if (in_user_cpuset (uid))
        return (PAM_SUCCESS);

    /*
     *  Read any configuration:
     */
    if (parse_options (conf, ac, av) < 0)
        return (PAM_SYSTEM_ERR);

    log_update (debuglevel, log_pam_syslog);

    /*
     *  If we didn't parse a config file due to "conf=" above,
     *   then parse the system config.
     */
    if (!cpuset_conf_file (conf))
        cpuset_conf_parse_system (conf);

    /*
     *  Now we have to create cpusets for all running jobs
     *   on the system for this user, so that they have the
     *   correct number of CPUs accounted to them upon logging 
     *   in. 
     */

    if ((lockfd = slurm_cpuset_create (conf)) < 0) {
        log_err ("Unable to initialilze slurm cpuset");
        return (PAM_SYSTEM_ERR);
    }
    
    /*
     *  create_all_job_cpusets returns the number of CPUs
     *   the user has allocated on this node (or -1 for failure)
     */

    if ((n = create_all_job_cpusets (conf, uid)) < 0) {
        log_err ("Failed to create user cpuset for uid=%d", uid);
        slurm_cpuset_unlock (lockfd);
        return (PAM_SYSTEM_ERR);
    }
    else if (n == 0) {
        log_err ("Access denied: User %s (uid=%d) has no active SLURM jobs.", 
                user, uid);
        slurm_cpuset_unlock (lockfd);
        return (PAM_PERM_DENIED);
    }

    if (migrate_to_user_cpuset (uid) < 0) {
        log_err ("Failed to create user cpuset for uid=%d", uid);
        slurm_cpuset_unlock (lockfd);
        return (PAM_SYSTEM_ERR);
    }
    slurm_cpuset_unlock (lockfd);

    log_msg ("Access granted for user %s (uid=%d) with %d CPUs", 
            user, uid, n);

    cpuset_conf_destroy (conf);

    return (PAM_SUCCESS);
}

static int in_user_cpuset (uid_t uid)
{
    char p [1024];
    char q [1024];
    int n;

    if (!cpuset_getcpusetpath (0, p, sizeof (p)))
        return (0);

    n = snprintf (q, sizeof (q), "/slurm/%d", uid);
    if ((n <= 0) || (n >= sizeof (q)))
        return (0);

    return (strncmp (p, q, strlen (q)) == 0);
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

int hostname_hostid (const char *host, const char *nodes)
{
    int n;
    hostlist_t h = hostlist_create (nodes);

    if (!(h = hostlist_create (nodes)))
        return (0);

    n = hostlist_find (h, host);
    hostlist_destroy (h);

    return (n);
}

int cpus_on_node (job_info_t *j, const char *host)
{
    return slurm_job_cpus_allocated_on_node (j->job_resrcs, host);
}

int create_all_job_cpusets (cpuset_conf_t conf, uid_t uid)
{
    int i;
    char hostname[256];
    char *p;
    job_info_msg_t * msg;
    int total_cpus = 0;

    if (gethostname (hostname, sizeof (hostname)) < 0) {
        return (-1);
    }

    if ((p = strchr (hostname, '.')))
        *p = '\0';

    dyn_slurm_open ();
    if (slurm_load_jobs (0, &msg, SHOW_ALL|SHOW_DETAIL) < 0) {
        return (-1);
    }

    for (i = 0; i < msg->record_count; i++) {
        job_info_t *j = &msg->job_array[i];
        int ncpus;

        if ((j->user_id != uid) || (j->job_state != JOB_RUNNING))
            continue;

        if ((ncpus = cpus_on_node (j, hostname)) <= 0)
            continue;

        if (!job_cpuset_exists (j->job_id, j->user_id) &&
            create_cpuset_for_job (conf, j->job_id, j->user_id, ncpus) < 0) {
            log_err ("job %u: Failed to create cpuset: %m", j->job_id);
            continue;
        }

        total_cpus += ncpus;
    }

    slurm_free_job_info_msg (msg);

    dyn_slurm_close ();

    return (total_cpus);
}

/*
 * vi: ts=4 sw=4 expandtab
 */

