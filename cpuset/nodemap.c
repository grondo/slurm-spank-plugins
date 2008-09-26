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

#include <bitmask.h>
#include <cpuset.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <slurm/spank.h>

#include "log.h"
#include "list.h"
#include "util.h"
#include "conf.h"
#include "nodemap.h"


/*
 *  Description of one NUMA node on the system.
 */
struct node {
    int             nodeid;    /* The NUMA node id                         */
    int             ncpus;     /* Total Number of CPUs                     */
    int             navail;    /* Number of currently available CPUs       */
    struct bitmask *localcpus; /* Bitmask mapping local CPUs to global     */
    struct bitmask *usedcpus;  /* Bitmask of used cpus (size = ncpus)      */
    struct nodemap *map;       /* Pointer back to the nodemap              */
};

#define ALLOC_IDLE_MULTIPLE 0 /* Allocate idle nodes first if 
                                 ntasks is multiple of node size  */
#define ALLOC_IDLE_GT       1 /* Allocate idle nodes first if 
                                 ntasks is >= node size           */
#define ALLOC_NO_IDLE       2 /* Do not allocate idle nodes first */

struct policy {
    unsigned int reverse:1;
    unsigned int best_fit:1;
    unsigned int first_fit:1;
    unsigned int worst_fit:1;
    unsigned int alloc_idle_first:1;
    unsigned int alloc_idle_multiples_only:1;
};

static struct policy default_policy = {
    .best_fit = 1,
    .alloc_idle_first = 1,
};

/*
 *   Store the current mapping of CPUs to memory nodes as
 *    well as the currently in-use CPUs.
 */
struct nodemap {
    struct policy   policy;    /* Allocation policy: best fit, first fit... */

    int             nnodes;    /* Number of NUMA nodes                      */
    int             ncpus;     /* Total number of CPUs online               */
    int             navail;    /* Total number of CPUs currently available  */
    struct bitmask *usedcpus;  /* Bitmask of used CPUs                      */
    struct bitmask *cpus;      /* Bitmask of available CPUs relative to the 
                                   current cpuset */ 
    List            nodelist;  /* List of nodes in this map                 */
};

/*
 *  A temporary object used to create a new allocation.
 */
struct allocation {
    int              ntasks;   /* Number of total tasks to allocate         */
    int              nleft;    /* Number of CPUs left to allocate           */
    struct nodemap * map;      /* pointer back to nodemap                   */
    struct bitmask * allocated_cpus;
                               /* The final bitmask of allocated CPUs       */
};

int nodemap_policy_update (struct nodemap *map, cpuset_conf_t cf)
{
    map->policy.best_fit = cpuset_conf_policy (cf) == BEST_FIT;
    map->policy.worst_fit = cpuset_conf_policy (cf) == WORST_FIT;
    map->policy.first_fit = cpuset_conf_policy (cf) == FIRST_FIT;
    map->policy.alloc_idle_first = cpuset_conf_alloc_idle (cf);
    map->policy.alloc_idle_multiples_only = 
        cpuset_conf_alloc_idle_multiple (cf);
    map->policy.reverse = cpuset_conf_reverse_order (cf);
    return (0);
}


static struct bitmask *current_cpuset_cpus ()
{
    struct bitmask *cpus;
    struct cpuset *cp;
   
   if ((cp = cpuset_alloc ()) == NULL) {
       cpuset_error ("Failed to alloc cpuset: %s\n", strerror (errno));
       return (NULL);
   }

   if ((cpus = bitmask_alloc (cpumask_size ())) == NULL) {
       cpuset_error ("Failed to alloc bitmask: %s\n", strerror (errno));
       cpuset_free (cp);
       return (NULL);
   }
   
   cpuset_query (cp, ".");
   cpuset_getcpus (cp, cpus);
   cpuset_free (cp);

   return (cpus);
}

static struct bitmask *used_cpus_bitmask ()
{
    return (used_cpus_bitmask_path (NULL, 0));
}

static struct node * node_create (struct nodemap *map, int id)
{
    int i, offset;
    struct bitmask *mems;
    struct node *n = malloc (sizeof (*n));

    if (n == NULL)
        return (NULL);


    n->map = map;

    n->nodeid = id;
    n->ncpus = 0;
    n->localcpus = bitmask_alloc (cpumask_size ());

    /*
     *  Get the bitmask of local cpus for this node
     */
    mems = bitmask_alloc (memmask_size ());
    bitmask_setbit (mems, n->nodeid);
    cpuset_localcpus (mems, n->localcpus);
    bitmask_free (mems);

    /*
     *  Now count the number of local CPUs
     */
    n->ncpus = bitmask_weight (n->localcpus);


    /*
     *  Now set used cpus from node map
     */
    n->usedcpus = bitmask_alloc (n->ncpus);

    offset = bitmask_first (n->localcpus);
    for (i = 0; i < n->ncpus; i++) {
        if (bitmask_isbitset (map->usedcpus, offset + i))
            bitmask_setbit (n->usedcpus, i);
    }

    n->navail = n->ncpus - bitmask_weight (n->usedcpus);

    cpuset_debug2 ("Done creating node%d with %d/%d CPUs\n",
            n->nodeid, n->navail, n->ncpus);

    return (n);
}

static void node_destroy (struct node *n)
{
    bitmask_free (n->localcpus);
    bitmask_free (n->usedcpus);
    free (n);
}


void nodemap_destroy (struct nodemap *map)
{
    bitmask_free (map->usedcpus);
    list_destroy (map->nodelist);
    free (map);
}

static int node_cpus_available (struct nodemap *map, int i)
{
    int rc;
    struct bitmask * mems = bitmask_alloc (memmask_size ());

    if (mems == NULL)
        return log_err ("failed to allocate mems mask!!\n");

    if (cpuset_localmems (map->cpus, mems) < 0)
        return log_err ("cpuset_localmems: %s\n", strerror (errno));

    rc = bitmask_isbitset (mems, i);

    bitmask_free (mems);
    return (rc);
}

struct nodemap * nodemap_create (cpuset_conf_t cf, struct bitmask *used)
{
    int i;
    struct nodemap *map = malloc (sizeof (*map));

    if (map == NULL)
        return (NULL);

    map->policy = default_policy;

    map->nodelist = list_create ((ListDelF) node_destroy);

    map->nnodes = memmask_size ();
    map->ncpus = cpumask_size ();

    if (used) {
        map->usedcpus = bitmask_alloc (bitmask_weight (used));
        bitmask_copy (map->usedcpus, used);
    }
    else {
        map->usedcpus = used_cpus_bitmask ();
    }

    if (!map->usedcpus) {
        list_destroy (map->nodelist);
        free (map);
        return (NULL);
    }

    map->cpus = current_cpuset_cpus ();

    for (i = 0; i < map->nnodes; i++) {
        struct node *n;

        /*
         *  Don't bother appending this node if none of its CPUs
         *   are available in the current cpuset
         */
        if (!node_cpus_available (map, i))
            continue;

        if ((n  = node_create (map, i)) == NULL) {
            nodemap_destroy (map);
            return (NULL);
        }
        list_push (map->nodelist, n);
    }

    map->navail = map->ncpus - bitmask_weight (map->usedcpus);

    log_debug2 ("Created nodemap with %d nodes, %d/%d CPUs\n",
            map->nnodes, map->navail, map->ncpus);

    nodemap_policy_update (map, cf);

    return (map);
}

void print_nodemap (const struct nodemap *map)
{
    struct node *n;
    struct bitmask *b;
    ListIterator i = list_iterator_create (map->nodelist);

    print_bitmask ("Available CPUs: %s\n", map->cpus);

    b = bitmask_alloc (cpumask_size ());
    bitmask_and (b, map->cpus, map->usedcpus);

    print_bitmask ("Used CPUs:      %s\n", b);
    bitmask_free (b);

    while ((n = list_next (i))) {
        //slurm_info ("Node%d:", n->nodeid);
        print_bitmask ("Local CPUs: %s\n", n->localcpus);
        print_bitmask ("Used CPUs:  %s\n", n->usedcpus);
    }

    list_iterator_destroy (i);
}


static int find_multiple_of_node_size (struct node *n, int *np)
{
    if (!(*np % n->ncpus) && (n->navail == n->ncpus))
        return (1);
    return (0);
}

static int find_node_lt_size (struct node *n, int *np)
{
    if ((*np >= n->ncpus) && (n->navail == n->ncpus))
        return (1);
    return (0);
}

static int should_allocate_idle_nodes (struct nodemap *m, int count)
{
    ListFindF fn;

    log_debug ("should_allocate_idle_nodes: %d\n", m->policy.alloc_idle_first);

    if (!m->policy.alloc_idle_first)
        return (0);

    if (m->policy.alloc_idle_multiples_only)
        fn = (ListFindF) find_multiple_of_node_size;
    else 
        fn = (ListFindF) find_node_lt_size;

    if (list_find_first (m->nodelist, fn, &count))
        return (1);
    return (0);
}

static struct allocation * allocation_create (struct nodemap *map, int ntasks)
{
    struct allocation *a = malloc (sizeof (*a));

    if (a == NULL)
        return (NULL);

    a->map = map;

    a->ntasks = a->nleft = ntasks;
    a->allocated_cpus = bitmask_alloc (cpumask_size ());

    return (a);
}

static void allocation_destroy (struct allocation *a)
{
    free (a);
}

static int node_cpu_to_global (struct node *n, int cpu)
{
    int firstcpu = bitmask_first (n->localcpus);
    return (firstcpu + cpu);
}

static int node_allocate_cpu (struct node *n, int cpu)
{
    int gcpu;
    if (bitmask_isbitset (n->usedcpus, cpu))
        return (-1);

    gcpu = node_cpu_to_global (n, cpu);
    if (bitmask_isbitset (n->map->usedcpus, gcpu))
        return (-1);

    bitmask_setbit (n->usedcpus, cpu);
    n->navail--;
    bitmask_setbit (n->map->usedcpus, gcpu);
    n->map->navail--;
    return (gcpu);
}

static void allocation_add_cpu (struct allocation *a, int cpu)
{
    bitmask_setbit (a->allocated_cpus, cpu);
    a->nleft--;
}

static int try_alloc (struct node *n, struct allocation *a, int cpu)
{
    int globalcpu = node_allocate_cpu (n, cpu);

    if (globalcpu < 0) /* CPU is in use */
        return (-1);

    cpuset_debug2 ("Node%d: allocated local CPU%d = CPU%d\n", 
                   n->nodeid, cpu, globalcpu);

    allocation_add_cpu (a, globalcpu);

    return (0);
}

static int node_allocate_n (struct node *n, struct allocation *a, int count)
{
    int nalloc = 0;
    int i;

    if (a->nleft == 0)
        return (0);

    /*
     *  Allocate all CPUs left in node if count == -1
     */
    if (count < 0)
        count = n->navail;

    cpuset_debug2 ("Allocating %d CPUs from node%d. nleft = %d\n", 
            count, n->nodeid, a->nleft);

    if (!n->map->policy.reverse) {
        /*
         *  Start with first CPU in node
         */
        for (i = 0; i < n->ncpus && a->nleft && nalloc < count; i++) {
            if (try_alloc (n, a, i) < 0)
                continue;
            nalloc++;
        }
    }
    else {
        /*
         *  Start with last CPU in node
         */
        for (i = n->ncpus - 1; i >= 0 && a->nleft && nalloc < count; i--) {
            if (try_alloc (n, a, i) < 0)
                continue;
            nalloc++;
        }
    }

    return (nalloc);
}

static int node_allocate_all (struct node *n, struct allocation *a)
{
    return (node_allocate_n (n, a, -1));
}

static int alloc_idle_nodes (struct allocation *a)
{
    ListIterator i;
    struct node *n;
    int nalloc = 0;

    cpuset_debug ("Attempting to allocate idle nodes\n"); 
    i = list_iterator_create (a->map->nodelist);

    while ((n = list_next (i)) && (a->nleft > 0)) {

        log_debug2 ("alloc_idle: node%d; avail=%d\n", n->nodeid, n->navail);
        if(n->navail == 0)
            continue;

        /*
         *  Ignore this node if we're only allocating multiples
         *   and the number of tasks left is not a multiple.
         */
        if (a->map->policy.alloc_idle_multiples_only
           && ((a->nleft % n->navail) != 0))
            continue;

        /*
         *  Otherwise, allocate whole, idle node.
         */
        if ((n->navail == n->ncpus) && (a->nleft >= n->navail)) {
            log_debug2 ("Allocating up to %d CPUs from node%d\n",
                    n->ncpus, n->nodeid);
            nalloc += node_allocate_n (n, a, n->ncpus);
        }
    }

    return (nalloc);
}

static int node_cmp_free (struct node *n1, struct node *n2)
{
    if (n1->navail == n2->navail)
        return (0);
    else if (n1->navail < n2->navail)
        return (-1);
    else
        return (1);
}

static int node_cmp_avail (struct node *n1, struct node *n2)
{
    int rc = node_cmp_free (n1, n2);
    return (-rc);
}

static int node_cmp_nodeid (struct node *n1, struct node *n2)
{
    if (n1->nodeid < n2->nodeid)
        return (-1);
    else if (n1->nodeid > n2->nodeid)
        return (1);
    else /* Shouldn't happen, but we'll check anyway */
        return (0);
}

static int node_cmp_reverse (struct node *n1, struct node *n2)
{
    return (-node_cmp_nodeid (n1, n2));
}


static int do_allocation (struct allocation *a, ListCmpF sort_f)
{
    if (sort_f)
        list_sort (a->map->nodelist, sort_f);
    list_for_each (a->map->nodelist, (ListForF) node_allocate_all, a);
    return (0);
}

static int allocation_best_fit (struct allocation *a)
{
    ListCmpF fn;

    log_debug ("allocation: best-fit\n");
    /*
     *  Best fit:
     *
     *  Sort NUMA nodes by amount of CPUs free in ascending 
     *   order, then pack in first-fit mode.
     */
    fn = (ListCmpF) node_cmp_free;

    return (do_allocation (a, fn));
}

static int allocation_first_fit (struct allocation *a)
{
    log_debug ("allocation: first-fit\n");
    return (do_allocation (a, NULL));
}

static int allocation_worst_fit (struct allocation *a)
{
    log_debug ("allocation: worst-fit\n");
    while (a->nleft) {
        /*
         *  For worst-fit, we have to sort by available CPUs in
         *   desending order, then allocate 1 CPU. Then re-sort, and
         *   so on.
         */
        list_sort (a->map->nodelist, (ListCmpF) node_cmp_avail);
        if (node_allocate_n (list_peek (a->map->nodelist), a, 1) < 0)
            return (-1);
    }
    return (0);
}

struct bitmask * nodemap_allocate (struct nodemap *map, int ncpus)
{
    struct bitmask *allocated;
    struct allocation * a;

    log_debug ("nodemap_allocate (ncpus=%d, navail=%d)\n",
            ncpus, map->navail);

    if (ncpus > map->navail) {
        cpuset_error ("%d CPUs requested, but only %d available\n",
                ncpus, map->navail);
        return (NULL);
    }

    if (!map->policy.reverse)
        list_sort (map->nodelist, (ListCmpF) node_cmp_nodeid);
    else
        list_sort (map->nodelist, (ListCmpF) node_cmp_reverse);

    if ((a = allocation_create (map, ncpus)) == NULL)
        return (NULL);

    if (should_allocate_idle_nodes (a->map, ncpus))
        alloc_idle_nodes (a);

    if (a->nleft > 0) {
        /*
         *  Allocate based on policy.
         */
        if (a->map->policy.best_fit)
            allocation_best_fit (a);
        else if (a->map->policy.first_fit)
            allocation_first_fit (a);
        else if (a->map->policy.worst_fit)
            allocation_worst_fit (a);
    }

    if (a->nleft > 0)
        cpuset_error ("Failed to allocate %d tasks.\n", a->nleft);

    allocated = a->allocated_cpus;
    a->allocated_cpus = NULL;

    allocation_destroy (a);

    return (allocated);
}

const struct bitmask * nodemap_used (struct nodemap *map)
{
    return (map->usedcpus);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
