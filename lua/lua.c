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
#include <dlfcn.h>
#include <setjmp.h> /* need longjmp for lua_atpanic */
#include <glob.h>

#include <slurm/spank.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lib/list.h"

SPANK_PLUGIN (lua, 1)

/*  Name of spank_t lightuserdata reference in
 *   spank table passed to lua spank functions.
 */
#define SPANK_REFNAME "spank"


/*
 *  This module keeps a list of options provided by the lua
 *   script so that it can easily map the option val (s_opt.val)
 *   back to the lua fucntion and val (l_function and l_val)
 *
 *  This is only necessary because SLURM's spank option callbacks
 *   do not provide any state or context besides a plugin-specific
 *   option value.
 *
 */
struct lua_script_option {
    lua_State *           L;
    int                   l_val;
    char *                l_function;
    struct spank_option   s_opt;
};

/*
 *  Global lua State and script_option_list declarations:
 */
static lua_State *global_L = NULL;
static List script_option_list = NULL;

/*
 *  Tell lua_atpanic where to longjmp on exceptions:
 */
static jmp_buf panicbuf;

/*
 *  Lua scripts pass string versions of spank_item_t to get/set_time.
 *   This table maps the name to item and vice versa.
 */
#define SPANK_ITEM(x)  { (x), #x }
#define SPANK_ITEM_END { 0, NULL }
static struct s_item_name {
    spank_item_t item;
    const char *name;
} spank_item_table [] = {
    SPANK_ITEM(S_JOB_UID),
    SPANK_ITEM(S_JOB_GID),
    SPANK_ITEM(S_JOB_ID),
    SPANK_ITEM(S_JOB_STEPID),
    SPANK_ITEM(S_JOB_NNODES),
    SPANK_ITEM(S_JOB_NODEID),
    SPANK_ITEM(S_JOB_LOCAL_TASK_COUNT),
    SPANK_ITEM(S_JOB_TOTAL_TASK_COUNT),
    SPANK_ITEM(S_JOB_NCPUS),
    SPANK_ITEM(S_JOB_ARGV),
    SPANK_ITEM(S_JOB_ENV),
    SPANK_ITEM(S_TASK_ID),
    SPANK_ITEM(S_TASK_GLOBAL_ID),
    SPANK_ITEM(S_TASK_EXIT_STATUS),
    SPANK_ITEM(S_TASK_PID),
    SPANK_ITEM(S_JOB_PID_TO_GLOBAL_ID),
    SPANK_ITEM(S_JOB_PID_TO_LOCAL_ID),
    SPANK_ITEM(S_JOB_LOCAL_TO_GLOBAL_ID),
    SPANK_ITEM(S_JOB_GLOBAL_TO_LOCAL_ID),
    SPANK_ITEM(S_JOB_SUPPLEMENTARY_GIDS),
    SPANK_ITEM(S_SLURM_VERSION),
    SPANK_ITEM(S_SLURM_VERSION_MAJOR),
    SPANK_ITEM(S_SLURM_VERSION_MINOR),
    SPANK_ITEM(S_SLURM_VERSION_MICRO),
    SPANK_ITEM(S_STEP_CPUS_PER_TASK),
    SPANK_ITEM(S_JOB_ALLOC_CORES),
    SPANK_ITEM(S_JOB_ALLOC_MEM),
    SPANK_ITEM_END
};


/*****************************************************************************
 *
 *  Lua script interface functions:
 *
 ****************************************************************************/

static int l_spank_error_msg (lua_State *L, const char *msg)
{
    lua_pushnil (L);
    lua_pushstring (L, msg);
    return (2);
}

static int l_spank_error (lua_State *L, spank_err_t e)
{
    return l_spank_error_msg (L, spank_strerror (e));
}


/*
 *  Get lua function return code.
 *  Functions must return nil for failure, anything else
 *   indicates success.
 */
static int lua_script_rc (lua_State *L)
{
    int rc = 0;
    if (lua_isnil (L, -1))
        rc = -1;
    if (lua_isnumber (L, -1))
        rc = lua_tonumber (L, -1);
    /* Clean up the stack */
    lua_pop (L, 0);
    return (rc);
}


static spank_t lua_getspank (lua_State *L, int index)
{
    spank_t sp;

    if (!lua_istable (L, 1))
        luaL_error (L, "Invalid argument expected table got %s",
                luaL_typename (L, 1));

    lua_getfield (L, index, SPANK_REFNAME);
    if (!lua_islightuserdata (L, -1))
        return (NULL);

    sp = lua_touserdata (L, -1);
    lua_pop (L, 1);
    return (sp);
}

static int name_to_item (const char *name)
{
    struct s_item_name *s = spank_item_table;

    while (s->name != NULL) {
        if (strcmp (s->name, name) == 0) {
            return (s->item);
        }
        s++;
    }
    return (-1);
}

static int l_spank_get_item_val (lua_State *L, spank_t sp, spank_item_t item)
{
    spank_err_t err;
    long val;

    err = spank_get_item (sp, item, &val);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushnumber (L, val);
    return (1);
}

static int
l_spank_get_item_string (lua_State *L, spank_t sp, spank_item_t item)
{
    spank_err_t err;
    const char *s;

    err = spank_get_item (sp, item, &s);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);
    lua_pushstring (L, s);
    return (1);

}

static int l_spank_get_item_argv (lua_State *L, spank_t sp)
{
    spank_err_t err;
    const char **av;
    int i, ac;

    err = spank_get_item (sp, S_JOB_ARGV, &ac, &av);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_newtable (L);
    for (i = 0; i < ac; i++) {
        lua_pushstring (L, av[i]);
        lua_rawseti (L, -2, i+1);
    }
    return (1);
}

static void set_env_table_entry (lua_State *L, int i, const char *entry)
{
    const char *val = strchr (entry, '=');
    if (val == NULL) {
        lua_pushstring (L, entry);
        lua_pushstring (L, "");
    }
    else {
        lua_pushlstring (L, entry, val - entry);
        lua_pushstring (L, val+1);
    }
    lua_settable (L, i);
}

static int l_spank_get_item_env (lua_State *L, spank_t sp)
{
    spank_err_t err;
    const char **env;
    const char **p;
    int t;

    err = spank_get_item (sp, S_JOB_ENV, &env);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_newtable (L);
    t = lua_gettop (L);

    for (p = env; *p != NULL; p++)
        set_env_table_entry (L, t, *p);

    return (1);
}

static int l_spank_get_item_gids (lua_State *L, spank_t sp)
{
    spank_err_t err;
    gid_t *gids;
    int i, ngids;

    err = spank_get_item (sp, S_JOB_SUPPLEMENTARY_GIDS, &gids, &ngids);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_newtable (L);
    for (i = 0; i < ngids; i++) {
        lua_pushnumber (L, gids[i]);
        lua_rawseti (L, -2, i+1);
    }
    return (1);
}

static int
l_spank_id_query (lua_State *L, spank_t sp, spank_item_t item)
{
    spank_err_t err;
    long rv, id;

    id = luaL_checknumber (L, -1);
    lua_pop (L, 1);

    err = spank_get_item (sp, item, id, &rv);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushnumber (L, rv);
    return (1);
}

static int l_spank_get_item (lua_State *L)
{
    spank_t sp;
    int item;

    sp = lua_getspank (L, 1);
    item = name_to_item (lua_tostring (L, 2));
    if (item < 0)
        return luaL_error (L,"Invalid spank item %s", lua_tostring (L, 2));

    lua_pop (L, 2);

    switch (item) {
        case S_JOB_UID:
        case S_JOB_GID:
        case S_JOB_ID:
        case S_JOB_STEPID:
        case S_JOB_NNODES:
        case S_JOB_NODEID:
        case S_JOB_LOCAL_TASK_COUNT:
        case S_JOB_TOTAL_TASK_COUNT:
        case S_JOB_NCPUS:
        case S_TASK_ID:
        case S_TASK_GLOBAL_ID:
        case S_TASK_EXIT_STATUS:
        case S_TASK_PID:
        case S_STEP_CPUS_PER_TASK:
        case S_JOB_ALLOC_MEM:
            return l_spank_get_item_val (L, sp, item);
        case S_JOB_ALLOC_CORES:
        case S_SLURM_VERSION:
        case S_SLURM_VERSION_MAJOR:
        case S_SLURM_VERSION_MINOR:
        case S_SLURM_VERSION_MICRO:
            return l_spank_get_item_string (L, sp, item);
        case S_JOB_ARGV:
            return l_spank_get_item_argv (L, sp);
        case S_JOB_ENV:
            return l_spank_get_item_env (L, sp);
        case S_JOB_SUPPLEMENTARY_GIDS:
            return l_spank_get_item_gids (L, sp);
        case S_JOB_PID_TO_GLOBAL_ID:
        case S_JOB_PID_TO_LOCAL_ID:
            return l_spank_id_query (L, sp, item);
    }

    return (0);
}

typedef spank_err_t (*setenv_f) (spank_t, const char *, const char *, int);
typedef spank_err_t (*getenv_f) (spank_t, const char *, char *, int);
typedef spank_err_t (*unsetenv_f) (spank_t, const char *);


static int l_do_getenv (lua_State *L, getenv_f fn)
{
    spank_err_t err;
    spank_t sp;
    const char *var;
    char buf[4096];

    sp = lua_getspank (L, 1);
    var = luaL_checkstring (L, 2);

    err = (*fn) (sp, var, buf, sizeof (buf));
    lua_pop (L, 0);

    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushstring (L, buf);
    return (1);
}

static int l_do_setenv (lua_State *L, setenv_f fn)
{
    spank_t sp;
    const char *var;
    const char *val;
    int overwrite;
    int err;

    sp = lua_getspank (L, 1);
    var = luaL_checkstring (L, 2);
    val = luaL_checkstring (L, 3);
    overwrite = lua_tonumber (L, 4); /* 0 by default */

    err = (*fn) (sp, var, val, overwrite);
    lua_pop (L, 0);

    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushboolean (L, 1);
    return (1);
}

static int l_do_unsetenv (lua_State *L, unsetenv_f fn)
{
    int err = (*fn) (lua_getspank (L, 1), luaL_checkstring (L, 2));
    lua_pop (L, 2);

    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushboolean (L, 1);
    return (1);
}

static int l_spank_setenv (lua_State *L)
{
    return l_do_setenv (L, spank_setenv);
}

static int l_spank_getenv (lua_State *L)
{
    return l_do_getenv (L, spank_getenv);
}

static int l_spank_unsetenv (lua_State *L)
{
    return l_do_unsetenv (L, spank_unsetenv);
}

static void * sym_lookup (const char *name)
{
    static void *h = NULL;
    if (h == NULL)
        h = dlopen (NULL, 0);
    return dlsym (h, name);
}

static int l_spank_job_control_setenv (lua_State *L)
{
    setenv_f f = sym_lookup ("spank_job_control_setenv");

    if (f == NULL)
        return l_spank_error_msg (L,
                "spank_job_control_setenv not implemented in this version");

    return l_do_setenv (L, f);
}
static int l_spank_job_control_getenv (lua_State *L)
{
    getenv_f f = sym_lookup ("spank_job_control_getenv");

    if (f == NULL)
        return l_spank_error_msg (L,
                "spank_job_control_getenv not implemented in this version");

    return l_do_getenv (L, f);
}
static int l_spank_job_control_unsetenv (lua_State *L)
{
    unsetenv_f f = sym_lookup ("spank_job_control_unsetenv");

    if (f == NULL)
        return l_spank_error_msg (L,
                "spank_job_control_unsetenv not implemented in this version");

    return l_do_unsetenv (L, f);
}

static int s_opt_find (struct lua_script_option *o, int *pval)
{
    return (o->s_opt.val == *pval);
}

static int lua_spank_option_callback (int val, const char *optarg, int remote)
{
    lua_State *L;
    struct lua_script_option *o;

    o = list_find_first (
            script_option_list,
            (ListFindF) s_opt_find,
            &val);

    if (o == NULL)
        return (-1);

    L = o->L;

    lua_getglobal (L, o->l_function);
    lua_pushnumber (L, o->l_val);
    lua_pushstring (L, optarg);
    lua_pushboolean (L, remote);

    if (lua_pcall (L, 3, 1, 0) != 0) {
        slurm_error ("Failed to call lua callback function %s: %s",
                    o->l_function, lua_tostring (L, -1));
        lua_pop (L, 1);
        return (-1);
    }

    return lua_script_rc (L);
}


static struct lua_script_option *lua_script_option_create (lua_State *L, int i)
{
    struct lua_script_option *o = malloc (sizeof (*o));

    if (o == NULL)
        luaL_error (L, "Unable to create lua script option: Out of memory");

    o->s_opt.cb = (spank_opt_cb_f) lua_spank_option_callback;
    o->L = L;

    /*
     *  Option name:
     */
    lua_getfield (L, i, "name");
    if (lua_isnil (L, -1))
        luaL_error (L, "Required field \"name\" missing from spank option table");
    o->s_opt.name = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);

    /*
     *  Option arginfo (optional):
     */
    lua_getfield (L, i, "arginfo");
    if (lua_isnil (L, -1))
        o->s_opt.arginfo = NULL;
    else
        o->s_opt.arginfo = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);

    /*
     *  Option usage (required):
     */
    lua_getfield (L, i, "usage");
    if (lua_isnil (L, -1))
        luaL_error (L, "Required field \"usage\" missing from spank option table");
    o->s_opt.usage = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);

    /*
     *  Option has_arg (optional):
     */
    lua_getfield (L, i, "has_arg");
    if (lua_isnil (L, -1))
        o->s_opt.has_arg = 0;
    else
        o->s_opt.has_arg = lua_tonumber (L, -1);
    lua_pop (L, 1);

    /*
     *  Option val (optional):
     */
    lua_getfield (L, i, "val");
    if (lua_isnil (L, -1))
        o->l_val = 0;
    else
        o->l_val = lua_tonumber (L, -1);
    lua_pop (L, 1);

    /*
     *  Option callback function name (required):
     */
    lua_getfield (L, i, "cb");
    if (lua_isnil (L, -1))
        luaL_error (L, "Required field \"cb\" missing from spank option table");
    o->l_function = strdup (lua_tostring (L, -1));
    lua_pop (L, 1);

    /*
     *  Check for existence of callback function
     */
    lua_getglobal (L, o->l_function);
    if (!lua_isfunction (L, -1))
        luaL_error (L, "Unable to find spank option cb function %s",
                o->l_function);
    lua_pop (L, 1);

    return (o);
}

static void lua_script_option_destroy (struct lua_script_option *o)
{
    if (o->s_opt.name)
        free (o->s_opt.name);
    if (o->s_opt.arginfo)
        free (o->s_opt.arginfo);
    if (o->s_opt.usage)
        free (o->s_opt.usage);
    if (o->l_function)
        free (o->l_function);
    free (o);
}

static int lua_script_option_register (lua_State *L, spank_t sp, int index)
{
    spank_err_t err;
    struct lua_script_option *opt = lua_script_option_create (L, index);

    if (!script_option_list)
        script_option_list = list_create ((ListDelF)lua_script_option_destroy);

    opt->s_opt.val = list_count (script_option_list);
    list_push (script_option_list, opt);

    err = spank_option_register (sp, &opt->s_opt);
    if (err != ESPANK_SUCCESS)
        return l_spank_error (L, err);

    lua_pushboolean (L, 1);
    return (1);
}

static int l_spank_option_register (lua_State *L)
{
    int rc;
    spank_t sp;

    sp = lua_getspank (L, 1);
    if (!lua_istable (L, 2))
        return luaL_error (L,
                "Expected table argument to spank_option_register");

    rc = lua_script_option_register (L, sp, 2);
    lua_pop (L, 2);

    return (rc);
}

static int l_spank_context (lua_State *L)
{
    switch (spank_context ()) {
    case S_CTX_LOCAL:
        lua_pushstring (L, "local");
        break;
    case S_CTX_REMOTE:
        lua_pushstring (L, "remote");
        break;
    case S_CTX_ALLOCATOR:
        lua_pushstring (L, "allocator");
        break;
    case S_CTX_ERROR:
        lua_pushstring (L, "error");
        break;
    }

    return (1);
}

/*****************************************************************************
 *  SPANK table
 ****************************************************************************/

static const struct luaL_Reg spank_functions [] = {
    { "register_option",      l_spank_option_register },
    { "get_item",             l_spank_get_item },
    { "getenv",               l_spank_getenv },
    { "setenv",               l_spank_setenv },
    { "unsetenv",             l_spank_unsetenv },
    { "job_control_setenv",   l_spank_job_control_setenv },
    { "job_control_getenv",   l_spank_job_control_getenv },
    { "job_control_unsetenv", l_spank_job_control_unsetenv },
    { NULL,                   NULL },
};


static int lua_spank_table_create (lua_State *L, spank_t sp, int ac, char **av)
{
    const char *str;
    int i;

    lua_newtable (L);
    luaL_register (L, NULL, spank_functions);

    /*  Register spank handle as light userdata inside spank table:
     */
    lua_pushlightuserdata (L, sp);
    lua_setfield (L, -2, SPANK_REFNAME);

    l_spank_context (L);
    lua_setfield (L, -2, "context");

    if (spank_get_item (sp, S_SLURM_VERSION, &str) == ESPANK_SUCCESS) {
        lua_pushstring (L, str);
        lua_setfield (L, -2, "slurm_version");
    }

    lua_newtable (L);
    for (i = 1; i < ac; i++) {
        lua_pushstring (L, av[i]);
        lua_rawseti (L, -2, i);
    }
    lua_setfield (L, -2, "args");

    return (0);
}

static int lua_spank_call (lua_State *L, spank_t sp, const char *fn,
        int ac, char **av)
{
    /*
     * Missing functions are not an error
     */
    lua_getglobal (L, fn);
    if (lua_isnil (L, -1))
        return 0;

    /*
     * Create spank object to pass to spank functions
     */
    lua_spank_table_create (L, sp, ac, av);

    if (lua_pcall (L, 1, 1, 0)) {
        slurm_error ("spank/lua: %s: %s", fn, lua_tostring (L, -1));
        return (-1);
    }

    return lua_script_rc (L);
}

static int l_spank_log_msg (lua_State *L)
{
    int level = luaL_checknumber (L, 1);
    const char *msg = luaL_checkstring (L, 2);

    if (level == -1) {
        slurm_error (msg);
        lua_pushnumber (L, -1);
        return (1);
    }

    if (level == 0)
        slurm_info (msg);
    else if (level == 1)
        slurm_verbose (msg);
    else if (level == 2)
        slurm_debug (msg);
    return (0);
}

static int SPANK_table_create (lua_State *L)
{
    lua_newtable (L);
    lua_pushcfunction (L, l_spank_log_msg);
    lua_setfield (L, -2, "_log_msg");

    /*
     *  Create more user-friendly lua versions of SLURM log functions
     *   with lua.
     */
    luaL_loadstring (L, "SPANK._log_msg (-1, string.format(unpack({...})))");
    lua_setfield (L, -2, "log_error");

    luaL_loadstring (L, "SPANK._log_msg (0, string.format(unpack({...})))");
    lua_setfield (L, -2, "log_info");

    luaL_loadstring (L, "SPANK._log_msg (1, string.format(unpack({...})))");
    lua_setfield (L, -2, "log_verbose");

    luaL_loadstring (L, "SPANK._log_msg (2, string.format(unpack({...})))");
    lua_setfield (L, -2, "log_debug");

    /*
     *  SPANK.SUCCESS and SPANK.FAILURE
     */
    lua_pushnumber (L, -1);
    lua_setfield (L, -2, "FAILURE");

    lua_pushnumber (L, 0);
    lua_setfield (L, -2, "SUCCESS");

    lua_setglobal (L, "SPANK");
    return (0);
}

int load_spank_options_table (lua_State *L, spank_t sp)
{
    int t;

    lua_getglobal (L, "spank_options");
    if (lua_isnil (L, -1))
        return (0);

    /*
     *  Iterate through spank_options table, which should
     *   be a table of spank_option entries
     */
    t = lua_gettop (L);
    lua_pushnil (L);  /* push starting "key" on stack */
    while (lua_next (L, t) != 0) {
        /*
         *  If lua_script_option_register() returns 2, then it has
         *   pushed 2 args on the stack and thus has failed. We
         *   don't need to return the failure message back to SLURM,
         *   that has been printed by lua, but we pop the stack and
         *   return < 0 so that SLURM can detect failure.
         */
        if (lua_script_option_register (L, sp, -1) > 1) {
            lua_pop (L, -1);  /* pop everything */
            return (-1);
        }

        /*  On success, lua_script_option_register pushes a boolean onto
         *   the stack, so we need to pop 2 items: the boolean and
         *   the spank_option table itself. This leaves the 'key' on
         *   top for lua_next().
         */
        lua_pop (L, 2);
    }
    lua_pop (L, 1); /* pop 'spank_options' table */

    return (0);
}

int spank_atpanic (lua_State *L)
{
    longjmp (panicbuf, 0);
}

struct lua_script {
    char *path;
    lua_State *L;
    int ref;
};

List lua_script_list = NULL;

static struct lua_script * lua_script_create (lua_State *L, const char *path)
{
    struct lua_script *script = malloc (sizeof (*script));

    script->path = strdup (path);
    script->L = lua_newthread (L);
    script->ref = luaL_ref (L, LUA_REGISTRYINDEX);

    /*
     *  Now we need to redefine the globals table for this script/thread.
     *   this will keep each script's globals in a private namespace,
     *   (including all the spank callback functions).
     *   To do this, we define a new table in the current thread's
     *   state, and give that table's metatable an __index field that
     *   points to the real globals table, then replace this threads
     *   globals table with the new (empty) table.
     *
     */

    /*  New globals table */
    lua_newtable (script->L);

    /*  metatable for table on top of stack */
    lua_newtable (script->L);

    /*
     *  Now set metatable->__index to point to the real globals
     *   table. This way Lua will check the root global table
     *   for any nonexistent items in the current thread's global
     *   table.
     */
    lua_pushstring (script->L, "__index");
    lua_pushvalue (script->L, LUA_GLOBALSINDEX);
    lua_settable (script->L, -3);

    /*  Now set metatable for the new globals table */
    lua_setmetatable (script->L, -2);

    /*  And finally replace the globals table with the (empty)  table
     *   now at top of the stack
     */
    lua_replace (script->L, LUA_GLOBALSINDEX);

    return script;
}

static void lua_script_destroy (struct lua_script *s)
{
    free (s->path);
    luaL_unref (global_L, LUA_REGISTRYINDEX, s->ref);
    /* Only call lua_close() on the main lua state  */
    free (s);
}

static int ef (const char *p, int eerrno)
{
    slurm_error ("spank/lua: glob: %s: %s\n", p, strerror (eerrno));
    return (-1);
}

List lua_script_list_create (lua_State *L, const char *pattern)
{
    glob_t gl;
    size_t i;
    List l = NULL;
    char *copy = NULL;

    if (pattern == NULL)
        return (NULL);

    int rc = glob (pattern, GLOB_ERR, ef, &gl);
    switch (rc) {
        case 0:
            l = list_create ((ListDelF) lua_script_destroy);
            for (i = 0; i < gl.gl_pathc; i++) {
                struct lua_script * s;
                s = lua_script_create (L, gl.gl_pathv[i]);
                if (s == NULL) {
                    slurm_error ("lua_script_create failed for %s.",
                            gl.gl_pathv[i]);
                    continue;
                }
                list_push (l, s);
            }
            break;
        case GLOB_NOMATCH:
            break;
        case GLOB_NOSPACE:
            slurm_error ("spank/lua: glob(3): Out of memory");
        case GLOB_ABORTED:
            slurm_verbose ("spank/lua: cannot read dir %s: %m", pattern);
            break;
        default:
            slurm_error ("Unknown glob(3) return code = %d", rc);
            break;
    }

    globfree (&gl);

    return l;
}


int slurm_spank_init (spank_t sp, int ac, char *av[])
{
    ListIterator i;
    struct lua_script *script;
    int rc;

    if (ac == 0) {
        slurm_error ("spank/lua: Requires at least 1 arg");
        return (-1);
    }

    /*
     *  dlopen liblua to ensure that symbols from that lib are
     *   available globally (so lua doesn't fail to dlopen its
     *   DSOs
     */
    if (!dlopen ("liblua.so", RTLD_NOW | RTLD_GLOBAL)) {
        slurm_error ("spank/lua: Failed to open liblua.so");
        return (-1);
    }

    global_L = luaL_newstate ();
    luaL_openlibs (global_L);

    /*
     *  Set up handler for lua_atpanic() so lua doesn't exit() on us.
     */
    lua_atpanic (global_L, spank_atpanic);
    if (setjmp (panicbuf)) {
        slurm_error ("spank/lua: %s: %s: %s",
                av[0], lua_tostring (global_L, -1));
        return (-1);
    }

    /*
     *  Create the global SPANK table
     */
    SPANK_table_create (global_L);

    lua_script_list = lua_script_list_create (global_L, av[0]);
    if (lua_script_list == NULL) {
        slurm_verbose ("spank/lua: No files found in %s\n", av[0]);
        return (0);
    }


    i = list_iterator_create (lua_script_list);
    while ((script = list_next (i))) {

        slurm_info ("luaL_loadfile (%s)", script->path);
        if (luaL_loadfile (script->L, script->path)) {
            slurm_error ("spank/lua: %s", lua_tostring (script->L, -1));
            return (-1);
        }

        /*
         *  Compile the lua script
         */
        if (lua_pcall (script->L, 0, 0, 0)) {
            slurm_error ("spank/lua: %s", lua_tostring (script->L, -1));
            return (-1);
        }

        /*
         *  Load any options exported via a spank_options table
         */
        if (load_spank_options_table (script->L, sp) < 0)
            return (-1);

        /*
         *  Call slurm_spank_init from the lua script
         */
        rc = lua_spank_call (script->L, sp, "slurm_spank_init", ac, av);
    }
    list_iterator_destroy (i);
    return rc;
}

static int call_foreach (List l, spank_t sp, const char *name,
        int ac, char *av[])
{
    int rc = 0;
    struct lua_script *script;
    ListIterator i = list_iterator_create (l);

    while ((script = list_next (i))) {
        if (lua_spank_call (script->L, sp, name, ac, av) < 0)
            rc = -1;
    }

    list_iterator_destroy (i);
    return (rc);
}

/*****************************************************************************
 *
 *  SPANK interface:
 *
 ****************************************************************************/
int slurm_spank_init_post_opt (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_init_post_opt", ac, av);
}

int slurm_spank_local_user_init (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_local_user_init", ac, av);
}

int slurm_spank_user_init (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_user_init", ac, av);
}

int slurm_spank_task_init_privileged (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_task_init_privileged", ac, av);
}

int slurm_spank_task_init (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_task_init", ac, av);
}

int slurm_spank_task_post_fork (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp,
            "slurm_spank_task_post_fork", ac, av);
}

int slurm_spank_task_exit (spank_t sp, int ac, char *av[])
{
    return call_foreach (lua_script_list, sp, "slurm_spank_task_exit", ac, av);
}

int slurm_spank_exit (spank_t sp, int ac, char *av[])
{
    int rc = call_foreach (lua_script_list, sp, "slurm_spank_exit", ac, av);

    if (lua_script_list)
        list_destroy (lua_script_list);
    if (script_option_list)
        list_destroy (script_option_list);
    lua_close (global_L);
    return (rc);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
