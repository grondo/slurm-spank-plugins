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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>

#include <slurm/spank.h>

#include "use-env.h"
#include "list.h"
#include "split.h"
#include "log_msg.h"

#define NO_SEARCH_SYSTEM 1<<0
#define NO_SEARCH_USER   1<<1

SPANK_PLUGIN(use-env, 1)

/****************************************************************************
 *  Static Variables
 ****************************************************************************/

static int local_user_cb_supported = 0;  /* 1 if spank_local_user is avail*/

static int disable_use_env =  0;         /*  Disable the plugin           */
static int disable_in_task =  0;         /*  Don't run in task if nonzero */
static char * default_name = "default";  /*  Name of system default file  */
static List   env_list     = NULL;       /*  Global list of files to read */
static char * home         = NULL;       /*  $HOME                        */

/****************************************************************************
 *  Wrappers for spank environment manipulation
 ****************************************************************************/

static int use_env_setenv (spank_t, const char *, const char *, int);
static int use_env_unsetenv (spank_t, const char *);
static const char *use_env_getenv (spank_t, const char *);

static struct use_env_ops spank_env_ops = {
    (getenv_f)   use_env_getenv,
    (setenv_f)   use_env_setenv,
    (unsetenv_f) use_env_unsetenv
};

/****************************************************************************
 *  SPANK Options
 ****************************************************************************/

static int use_env_opt_process (int val, char *optarg, int remote);

struct spank_option spank_options[] = 
{
    { "use-env", "[name]",
      "Read env from ~/.slurm/environment/[name] or "
     "/etc/slurm/environment/[name]", 1, 0,
      (spank_opt_cb_f) use_env_opt_process
    },
    SPANK_OPTIONS_TABLE_END
};

/****************************************************************************
 *  Forward Declarations
 ****************************************************************************/

static int check_local_user_symbol ();
static int use_env_debuglevel ();
static int process_args (int ac, char **av);
static char * xgetenv_copy (const char *var);
static char * env_override_file_search (char *, size_t, const char *, int);
static int do_env_override (const char *path, spank_t sp);
static int define_all_keywords (spank_t sp);

/****************************************************************************
 *  SPANK Functions
 ****************************************************************************/

/*
 *   slurm_spank_init is called as root in slurmd, but I don't 
 *    think this matters in this case because all we do here
 *    is initialize the parser and search for default environment 
 *    override files. Maybe later this can be duplicated in 
 *    slurm_spank_user_init for safety.
 */
int slurm_spank_init (spank_t sp, int ac, char **av)
{
    char buf [4096];
    size_t len = sizeof (buf);

    check_local_user_symbol ();

    if (process_args (ac, av) < 0)
        return (-1);

    env_list = list_create ((ListDelF) free);

    /*
     *  Set environment access functions to spank versions
     *   if we're running remotely.
     */
    if (spank_remote (sp)) 
        use_env_set_operations (&spank_env_ops, sp);

    /*
     *  Initialize global HOME dir
     */
    if ((home = xgetenv_copy ("HOME")) == NULL) {
        /*
         *  If HOME variable is not set, and we're running as root,
         *   then be safe and just bail out here. This could be a
         *   salloc/sbatch process running on behalf of a batch system
         *   that uses the --uid=USER option. Since there is no way
         *   to determine what user the job is being submitted as
         *   (at this time), it is safest to bail out (and later
         *   ignore any --use-env options.
         */
        disable_use_env = 1;
        return 0;
    }

    /*
     *  Check for default files in the following order:
     *   /etc/slurm/environment/default || /etc/slurm/env-default.conf
     *   ~/.slurm/environment/default   || ~/.slurm/env-default.conf
     */
    if (env_override_file_search (buf, len, default_name, NO_SEARCH_USER))
        list_append (env_list, strdup (buf));
    
    /*
     *  Always use name "default" for user default environment
     */
    if (env_override_file_search (buf, len, "default", NO_SEARCH_SYSTEM))
        list_append (env_list, strdup (buf));

    /*
     *  Initialize logging and parser:
     */
    log_msg_init ("use-env");
    use_env_parser_init (spank_remote (sp));
    log_msg_set_verbose (use_env_debuglevel ());

    /*
     *  if we don't have the local_user callback, then we have 
     *   to instantiate the default environment here.
     */
    if (!local_user_cb_supported && !spank_remote (sp)) {
        list_for_each (env_list, (ListForF) do_env_override, NULL);
        list_destroy (env_list);
    }

    return (0);
}

int slurm_spank_local_user_init (spank_t sp, int ac, char **av)
{
    if (disable_use_env)
        return (0);

    if (define_all_keywords (sp) < 0)
        return (-1);

    list_for_each (env_list, (ListForF) do_env_override, NULL);
    list_destroy (env_list);

    return (0);
}

int slurm_spank_task_init (spank_t sp, int ac, char **av)
{
    /*
     * Reset operations to make sure the right spank handle is
     *  available.
     */
    use_env_set_operations (&spank_env_ops, sp);

    if (define_all_keywords (sp) < 0)
        return (-1);

    list_for_each (env_list, (ListForF) do_env_override, (void *) sp);
    list_destroy (env_list);
    return (0);
}

int slurm_spank_exit (spank_t sp, int ac, char **av)
{
    if (disable_use_env)
        return (0);

    use_env_parser_fini ();
    log_msg_fini ();
    return (0);
}

/****************************************************************************
 *  Static Functions
 ****************************************************************************/

static int check_local_user_symbol ()
{
    int (*sym_supported) (const char *);

    if (  (sym_supported = dlsym (NULL, "spank_symbol_supported"))
       && (*sym_supported) ("slurm_spank_local_user_init"))
        local_user_cb_supported = 1;
    else
        slurm_debug3 ("use-env: slurm_spank_local_user_init not supported");

    return (0);
}

static int use_env_debuglevel () 
{
    const char *val;
    int rv = 0;

    if ((val = xgetenv ("SPANK_USE_ENV_DEBUG"))) {
        char *p;
        long n = strtol (val, &p, 10);
        if (p && (*p == '\0')) 
            rv = n;
        else
            slurm_error ("Invalid value %s for SPANK_USE_ENV_DEBUG", val);
    }

    return (rv);
}

static char * 
env_override_file_search (char *path, size_t len, const char *name, int flags)
{
    int check_user = !(flags & NO_SEARCH_USER);
    int check_sys  = !(flags & NO_SEARCH_SYSTEM);

    if (home && check_user) {
        snprintf (path, len, "%s/.slurm/environment/%s", home, name);
        if (access (path, R_OK) >= 0) 
            return (path);
        snprintf (path, len, "%s/.slurm/env-%s.conf", home, name);
        if (access (path, R_OK) >= 0) {
            return (path);
        }
    }

    if (check_sys) {
        snprintf (path, len, "/etc/slurm/environment/%s", name);
        if (access (path, R_OK) >= 0)
            return (path);
        snprintf (path, len, "/etc/slurm/env-%s.conf", name);
        if (access (path, R_OK) >= 0)
            return (path);
    }

    return (NULL);
}

static int do_env_override (const char *path, spank_t sp)
{
    slurm_verbose ("use_env_parse (%s)", path);

    if (use_env_parse (path) < 0) {
        slurm_error ("--use-env: Errors reading %s\n", path);
        return (-1);
    }
    return (0);
}

static int path_cmp (char *x, char *y)
{
    return (strcmp (x, y) == 0);
}

static int check_and_append_env_opt (char *name, List l)
{
    int rc = 0;
    char buf [4096];
    size_t len = sizeof (buf);

    if (!env_override_file_search (buf, len, name, 0)) {
        slurm_error ("use-env: Unable to find env override file \"%s\"", name);
        return (-1);
    }

    /*
     *  If we don't have the local_user callback, then we have
     *   to call do_env_override immediately
     */
    if (!local_user_cb_supported) 
        rc = do_env_override (buf, NULL);
    else if (!list_find_first (env_list, (ListFindF) path_cmp, buf))
        list_append (env_list, strdup (buf));

    return (rc);
}

static int use_env_opt_process (int val, char *optarg, int remote) 
{
    List l;

    if  (optarg == NULL) {
        slurm_error ("--use-env: Invalid argument");
        return (-1);
    }

    l = list_split (",", optarg);
    if (list_for_each (l, (ListForF) check_and_append_env_opt, env_list) < 0)
        return (-1);
    list_destroy (l);

    return (0);
}

static int 
define_use_env_keyword (spank_t sp, char *name, spank_item_t item)
{
    int n;
    int val;
    char buf [64];

    if (spank_get_item (sp, item, &val) != ESPANK_SUCCESS) {
        slurm_error ("use-env: spank_get_item failed for %s\n", name);
        return (-1);
    }

    n = snprintf (buf, sizeof (buf), "%u", val);

    if ((n < 0) || (n >= sizeof (buf))) {
        slurm_error ("use-env: value of %s too large for buffer\n", name);
        return (-1);
    }

    if (keyword_define (name, buf) == NULL)
        return (-1);

    return (0);
}

static int set_argv_keywords (spank_t sp)
{
    char *cmdline;
    size_t cmdlen;
    char buf [64];
    const char **av;
    int ac;
    int i;
    int n;

    if (spank_get_item (sp, S_JOB_ARGV, &ac, &av) != ESPANK_SUCCESS) {
        slurm_error ("use-env: spank_get_item failed for argv");
        return (-1);
    }

    n = snprintf (buf, sizeof (buf), "%d", ac);

    if ((n < 0) || (n >= sizeof (buf))) {
        slurm_error ("use-env: value of ARGC too large");
        return (-1);
    }

    keyword_define ("SLURM_ARGC", buf);

    cmdlen = 0;
    for (i = 0; i < ac; i++) {

        snprintf (buf, sizeof (buf), "SLURM_ARGV%d", i);
        keyword_define (buf, av[i]);

        cmdlen += strlen (av[i]) + 1;
    }

    if ((cmdline = malloc (cmdlen + 1)) == NULL) {
        slurm_error ("use-env: Out of memory setting SLURM_CMDLINE!");
        return (-1);
    }
    cmdline[0] = '\0';


    /*
     *  Build SLURM_CMDLINE string:
     */
    strcat (cmdline, av[0]);
    for (i = 1; i < ac; i++) {
        strcat (cmdline, " ");
        strcat (cmdline, av[i]);
    }

    keyword_define ("SLURM_CMDLINE", cmdline);

    free (cmdline);

    return (0);
}

static int define_all_keywords (spank_t sp)
{
    /*
     *  These keywords are only accessible from this context
     */
    if (define_use_env_keyword (sp, "SLURM_NNODES", S_JOB_NNODES) < 0)
        return (-1);
    if (define_use_env_keyword (sp, "SLURM_NPROCS", S_JOB_TOTAL_TASK_COUNT) < 0)
        return (-1);
    if (define_use_env_keyword (sp, "SLURM_JOBID", S_JOB_ID) < 0)
        return (-1);
    if (define_use_env_keyword (sp, "SLURM_STEPID", S_JOB_STEPID) < 0)
        return (-1);

    if (set_argv_keywords (sp) < 0)
        return (-1);

    if (!spank_remote (sp))
        return (0);

    if (define_use_env_keyword (sp, "SLURM_PROCID", S_TASK_GLOBAL_ID) < 0)
        return (-1);
    if (define_use_env_keyword (sp, "SLURM_LOCALID", S_TASK_ID) < 0)
        return (-1);
    if (define_use_env_keyword (sp, "SLURM_NODEID", S_JOB_NODEID) < 0)
        return (-1);

    return (0);
}

static int process_args (int ac, char **av)
{
   int i;
    for (i = 0; i < ac; i++) {
        if (strncmp ("default=", av[i], 8) == 0)
            default_name = av[i] + 8;
        else if (strcmp ("disable_in_task", av[i]) == 0)
            disable_in_task = 1;
        else {
            slurm_error ("use-env: Invalid option \"%s\"", av[i]);
            return (-1);
        }
    }

    return (0);
}

static char * xgetenv_copy (const char *var)
{
    const char *val = xgetenv (var);
    if (val)
        return strdup (val);
    return (NULL);
}

/****************************************************************************
 *  Environment manipulation wrappers
 ****************************************************************************/

static const char *use_env_getenv (spank_t sp, const char *name)
{
    static char buf [4096];

    memset (buf, 0, sizeof (buf));
   
    if (spank_getenv (sp, name, buf, sizeof (buf)) != ESPANK_SUCCESS)
        return (NULL);

    return (buf);
}

static int use_env_unsetenv (spank_t sp, const char *name)
{
    if (spank_unsetenv (sp, name) != ESPANK_SUCCESS)
        return (-1);
    return (0);
}


static int use_env_setenv (spank_t sp, const char *name, const char *val,
                           int overwrite)
{
    if (spank_setenv (sp, name, val, overwrite) != ESPANK_SUCCESS && overwrite) 
        return (-1);
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
