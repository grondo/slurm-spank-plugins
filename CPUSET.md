
---

## SLURM CPUSET Plugin for CPU Scheduled Clusters ##

  * [DESCRIPTION](#DESCRIPTION.md)
  * [REQUIREMENTS](#REQUIREMENTS.md)
  * [SLURM PLUGIN](#SLURM_PLUGIN.md)
  * [PAM MODULE](#PAM_MODULE.md)
  * [RELEASE AGENT](#RELEASE_AGENT.md)
  * [CONFIGURATION](#CONFIGURATION.md)
  * [USER OPTIONS](#USER_OPTIONS.md)
  * [OPERATION](#OPERATION.md)
  * [EXAMPLES](#EXAMPLES.md)
  * [SEE ALSO](#SEE_ALSO.md)
    * [slurm-cpuset-info script](#slurm_cpuset_info_script.md)

---

### DESCRIPTION ###

The SLURM **cpuset** suite enables the use of Linux cpuset functionality
to constrain user jobs and login sessions to compute resources to the
number of CPUs allocated on nodes. The suite consists of a [spank](SPANKInfo.md)
plugin, a PAM module, and an optional cpuset release agent. Together,
these three components effectively restrict user access to shared nodes
based on actual SLURM allocations.

The SLURM cpuset components are specifically designed for systems
sharing nodes using CPU scheduling, that is systems using the
[consumable resources](http://computing.llnl.gov/linux/slurm/cons_res.html)
plugin for SLURM. The cpuset plugins and utilities will **not** be effective
on systens where CPUs may be oversubscribed to jobs (e.g. strict node
sharing without the use of _select_/_cons\_res_.


---

### REQUIREMENTS ###

The cpuset suite requires Linux cpusets support. It also uses
the _libbitmask_ and _libcpuset_ libraries from SGI for creating
and managing cpusets. (This dependency will probably be removed
int the future). Sources for these libs is available from

> http://oss.sgi.com/projects/cpusets

The cpuset filesystem must also be mounted at runtime in order for
the plugin to be able to query and create cpusets. To mount the cpuset
filesystem, use:

```
 mount -t cpuset none /dev/cpuset
```

The software currently assumes that the cpuset filesystem will be available
under `/dev/cpuset`.


---

### SLURM PLUGIN ###

The core cpuset functionality for SLURM jobs is provided by a SLURM
[spank](SPANKInfo.md) plugin **`cpuset.so`**. Since this plugin uses the
**spank** framework, it must be enabled in the **`plugstack.conf`** for
the system with the following line:

```
 required  cpuset.so  [options]
```


where options `[options]` are described further in the
[USER OPTIONS](#USER_OPTIONS.md) section below.

The  slurm  cpuset  plugin  (as  well as other SLURM cpuset components)
works on a single node. It knows nothing  about the global  state
of SLURM,  its queues, etc. Local CPUs are allocated dynamically to
incoming jobs based on the number of CPUs assigned to the job by SLURM.
The cpuset plugin does not keep any state across jobs, nor across the
nodes of a job. Instead, it uses past created cpusets to track which
CPUs are currently in use, and which are available.

The  SLURM  cpuset  plugin  may  also  constrain job steps to their own
cpusets under the job cpuset. This may be useful when running multiple job
steps under a single allocation, as the resources of each job step may be
partitioned into separate, non-overlapping cpusets.  This functionality
is enabled by the srun user option

```
    --use-cpusets=[args...]
```

Where  the optional arguments in args modify the cpuset plugin behavior
for job steps and/or tasks. Any  plugin  option  as  described  in  the
OPTIONS section can be specified.


---

### PAM MODULE ###

The  **pam\_slurm\_cpuset** module may be used to restrict user login ses-
sions on compute nodes to only the CPUs which they have been  allocated
by SLURM. If enabled in the PAM stack, it will also deny access to
users attempting to log in to nodes which they have not been allocated.

The  **pam\_slurm\_cpuset**  PAM  module uses the same configuration file and
algorithms as the SLURM cpuset plugin, and is further documented in the
**pam\_slurm\_cpuset**(8) man page.


---

### RELEASE AGENT ###

Included  with  the  SLURM  cpuset  utilities is a cpuset release-agent
which may optionally be installed  into  `/sbin/cpuset_release_agent`  on
any  nodes  using  the  SLURM cpuset plugin or PAM module. This release
agent will be run for each SLURM cpuset when the last task  within  the
cpuset exits, and will free the cpuset immediately (with proper locking
so as to not race with other jobs). The release agent is optional for a
couple reasons:

  1. In the current version of Linux for which this plugin was written (RHEL5), there can only be one release-agent system-wide. We don't want to interfere with other uses of cpusets if they exist.

  1. The cpuset plugin removes stale cpusets at startup anyway. So, the cpuset\_release\_agent is not a critical component. However, it is nice to clean up job cpusets as the jobs exit, instead of waiting until the **next** job is run. Unused cpusets lying around may be confusing to users and sysadmins.


---

### CONFIGURATION ###

All SLURM cpuset components will first attempt to read  the  systemwide
config file at **`/etc/slurm/slurm-cpuset.conf`**. This location may be
overridden in the PAM module and SLURM plugin  with  the  **`conf=`**  parameter.
However,  this  is  not suggested, because there is no way currently to
override the config file location for the cpuset release agent.

Available configuration parameters that may be set in slurm-cpuset.conf
are:

| **Parameter**                  | **Description**                                  |
|:-------------------------------|:-------------------------------------------------|
| **policy** = _POLICY_          | Set the allocation policy for cpuset to _POLICY_ |
| **order** = _normal_/_reverse_ | Set the allocation order of tasks to CPUs. In _normal_ mode, tasks are allocated starteing with the first available CPU and in increasing order, while with _reverse_ order, tasks are allocated starting with the last available CPU. The default order is _normal_. |
| **use-idle** = _STRATEGY_      | Indicate when to allocate tasks to fully idle NUMA nodes. The default behavior is to use idle nodes first when the number of tasks in the job is a multiple of the number of CPUs within a NUMA node. See below for other options. |
| **constrain-mem** = _BOOLEAN_  | If set to 1 or _yes_, constrain memory nodes along with CPUs when creating cpusets. If set to 0 or _no_, let all cpusets access all memory nodes on the system (i.e. do not constrain memory). The default is _yes_. |
| **kill-orphs** = _BOOLEAN_     | If set to 1 or _yest_, kill orphaned user logins, i.e. those login sessions for which there are no longer and SLURM jobs running. If 0 or _no_, then leave orhpan user logins alone. The default is _no_. |

Available allocation policies include:

| policy = **best-fit**  | Allopcate tasks to the most full NUMA nodes first. This is the default |
|:-----------------------|:-----------------------------------------------------------------------|
| policy = **first-fit** | Allocate tasks to NUMA nodes in order of node ID. |
| policy = **worst-fit** | Allocate tasks to least full NUMA nodes first |

Available strategies for **use-idle** include:

| use-idle = **multiple** | The default. Allocate idle nodes first if the number of tasks is a multiple of the node size |
|:------------------------|:---------------------------------------------------------------------------------------------|
| use-idle = **greater**  | Allocate idle nodes first if the number of tasks is greater than the number of CPUs in a NUMA node. |
| use-idle = **never**    | Do not allocate idle nodes first, no matter the job size. |
| use-idle = **yes**      | Allocate idle nodes first using the default **policy** |


---

### USER OPTIONS ###

The **`--use-cpusets`** option in **srun** may be used to override some of
the global options above, in addition to providing extra user-specific
options. Currently supported arguments for the **`--use-cpusets`** option
include (These options only apply to job step cpusets, not the overall
job cpusets, which are still controlled by global configuration, and
are not modifiable by user option)

| help     | Print a short usage message to stderr and exit. |
|:---------|:------------------------------------------------|
| debug    | Enable debug messages |
| debug=N  | Increase debug verbosity to _N_ |
| conf=_FILENAME_ |  Read configuration from _FILENAME_. Settings in this config file will override system configuration, as well as options previously set on command line |
| policy=_POLICY_ | Same as **policy** above |
| order=_ORDER_   | Set allocation order to _normal_ or _reverse_ |
| reverse | Same as order=_reverse_ |
| _POLICY_ | Shortcut for policy=_POLICY_ |
| idle-first=_WHEN_ | As above, set _WHEN_ to allocate idle nodes first |
| no-idle | Same as idle-first=_no_ |
| constrain-mem | constrain memory as well as CPUs |
| !constrain-mem | Do not constrain memory |
| tasks | Also constrain individual tasks to their own cpusets |


---

### OPERATION ###

All  SLURM  cpusets  for  jobs and login sessions are created under the
/slurm cpuset heirarchy, and require  that  the  cpuset  filesystem  be
mounted  under  /dev/cpuset  (An  init script is provided for this purpose.).

The first level of cpuset created under the /slurm  directory  are  **UID**
cpusets.  Each user with a job or login to the current node will have a
cpuset under

> `/slurm/UID`

which will contain the set of all CPUs that user is allowed to  use  on
the  system.  Processes which are part of a login session are contained
within this cpuset, and thus have access to all CPUs which the user has
been allocated.

Under each **UID** cpuset will be one cpuset per active job.  These cpusets
are named with the JOBID, and thus fall under the path

> `/slurm/UID/JOBID`

The CPUs allocated to the **JOBID** cpusets will obviously be a  subset  of
the **UID** cpuset.

Finally,  if  the user requests per-job-step or per-task cpusets, these
cpusets will fall under the **JOBID** cpuset, and will of course be a  sub-
set of the job cpuset. Thus, the final cpuset path for a task would be:

> `/slurm/UID/JOBID/STEPID/TASKID`

where there would be _N_ **TASKID** cpusets for an _N_ task job.

As  cpusets  are  created  by   the   SLURM   cpuset   utilities,   the
notify\_on\_release  flag is set. This causes the cpuset release agent at
`/sbin/cpuset_release_agent` to be called after the last task exits  from
the  cpuset.   The  SLURM  cpuset version of cpuset\_release\_agent takes
care of removing the cpuset and releasing CPUs for  use  if  necessary.
Use of the release agent is optional, however, because the SLURM cpuset
utilities will also try to free unused cpusets on demand as well.

The general algorithm the SLURM cpuset utilities use for  allocating  a
new **JOB** cpuset is as follows:

  1. Lock SLURM cpuset at /dev/cpuset/slurm.

  1. Clean  up  current  slurm  cpuset heirarchy by removing all unused cpusets, and ensuring user cpusets (/slurm/UID) are up to date.

  1. Check for an existing cpuset for this job in /slurm/UID/JOBID.  If it exists, goto directly to step 8.

  1. Scan  the  slurm cpuset heirarchy and gather the list of currently used CPUs. This is the union of all active user cpusets, which are in turn the union of all active user job cpusets.

  1. Abort  if  the  number  of  CPUs  assigned  to the starting job is greater than the number of available CPUs.

  1. Assign CPUs and optionally memory nodes  based  on  the  currently configured policy. (See CONFIGURATION section for valid policies)

  1. Create  new cpuset under /dev/cpuset/slurm/UID/JOBID, updating the user cpuset if necessary with newly allocated cpus.

  1. Migrate job to cpuset /dev/cpuset/slurm/UID/JOBID.

  1. Unlock SLURM cpuset at /dev/cpuset/slurm.


---

### EXAMPLES ###

Default allocation policy, job sizes 2 cpus, 1 cpu, 1 cpu, 4 cpus:
```
 cpuset: /slurm/6885/69946: 2 cpus [0-1], 1 mem [0]
 cpuset: /slurm/6885/69947: 1 cpu [2], 1 mem [1]
 cpuset: /slurm/6885/69948: 1 cpu [3], 1 mem [1]
 cpuset: /slurm/6885/69950: 4 cpus [4-7], 2 mems [2-3]
```

Same as above with order = reverse.
```
 cpuset: /slurm/6885/69954: 2 cpus [6-7], 1 mem [3]
 cpuset: /slurm/6885/69955: 1 cpu [5], 1 mem [2]
 cpuset: /slurm/6885/69956: 1 cpu [4], 1 mem [2]
 cpuset: /slurm/6885/69957: 4 cpus [0-3], 2 mems [0-1]
```

use-idle = never, policy = worst-fit: job sizes 1, 1, 1, 4, 1
```
 cpuset: /slurm/6885/69976: 1 cpu [0], 1 mem [0]
 cpuset: /slurm/6885/69977: 1 cpu [2], 1 mem [1]
 cpuset: /slurm/6885/69978: 1 cpu [4], 1 mem [2]
 cpuset: /slurm/6885/69979: 4 cpus [1,3,6-7], 3 mems [0-1,3]
 cpuset: /slurm/6885/69980: 1 cpu [5], 1 mem [2]
```

policy = first-fit: job sizes 1, 1, 1, 4, 1 Note  that  4  cpu  job  is
allocated to idle nodes first.
```
 cpuset: /slurm/6885/69985: 1 cpu [0], 1 mem [0]
 cpuset: /slurm/6885/69986: 1 cpu [1], 1 mem [0]
 cpuset: /slurm/6885/69987: 1 cpu [2], 1 mem [1]
 cpuset: /slurm/6885/69988: 4 cpus [4-7], 2 mems [2-3]
 cpuset: /slurm/6885/69989: 1 cpu [3], 1 mem [1]
```

Using cpusets for multiple job steps under an allocate of 1 node with 8
cpus.
```
 > srun --use-cpusets=debug -n1 sleep 100 &

  cpuset: /slurm/6885/69993: 8 cpus [0-7], 4 mems [0-3]
  cpuset: /slurm/6885/69993/0: 1 cpu [0], 1 mem [0]

 > srun --use-cpusets=debug -n2 sleep 100 &

  cpuset: /slurm/6885/69993: 8 cpus [0-7], 4 mems [0-3]
  cpuset: /slurm/6885/69993/1: 2 cpus [2-3], 1 mem [1]
```

Use of --use-cpusets=tasks
```
 > srun --use-cpusets=debug,tasks -n4 sleep 100

 cpuset: /slurm/6885/69993: 8 cpus [0-7], 4 mems [0-3]
 cpuset: /slurm/6885/69993/2: 4 cpus [0-3], 2 mems [0-1]
 cpuset: /slurm/6885/69993/2/0: 1 cpu [0], 1 mem [0]
 cpuset: /slurm/6885/69993/2/1: 1 cpu [1], 1 mem [0]
 cpuset: /slurm/6885/69993/2/2: 1 cpu [2], 1 mem [1]
 cpuset: /slurm/6885/69993/2/3: 1 cpu [3], 1 mem [1]
```


---

### SEE ALSO ###

This page may be out of date. For up-to-date information see the
man pages provided with this software: _slurm-cpuset(8)_,
_use-cpusets(1)_, and _pam\_slurm\_cpuset(8)_.

#### slurm-cpuset-info script ####

An example script for querying SLURM cpuset information is
included in the slurm-spank-plugins source. See

> [slurm-cpuset-info](http://slurm-spank-plugins.googlecode.com/svn/trunk/cpuset/slurm-cpuset-info)

The **slurm-cpuset-info** script requires that
[pdsh](http://computing.llnl.gov/linux/pdsh.html)
and the **Hostlist** perl module which is part of the
[gendersllnl](http://sourceforge.net/projects/genders) package be installed to work properly.

Examples:

```
Usage: slurm-cpuset-info [OPTIONS]...

Query general information about cpusets in use by SLURM jobs.  By default, 
  the program displays CPUs in use by all jobs on the current node, but 
  can optionally display concise per-node information or display CPUs
  in use by jobs across the system.

    -h, --help                 Display this usage message.
    -n, --nodes=LIST           Report on nodes in LIST (default = local node).
    -a, --all                  Report on all nodes or jobs.
    -j, --jobids=LIST          Only report on job ids in LIST.
    -u, --users=LIST           Only report on users in LIST.
    -p, --partitions=LIST      Only report on partitions in LIST.
        --exclude-ncpus=N      Exclude jobs with CPU count of N.
        --include-ncpus=N      Include only jobs with CPU count of N.
        --xn=N                 Synonym for --exclude-ncpus=N.
        --in=N                 Synonym for --include-ncpus=N.

    -N, --per-node             Print per node output instead of per job.
```

```
grondo@yanai ~ >slurm-cpuset-info -Na -p pbatch
        HOST PARTITION    NJOBS   USED               FREE
      yana14 pbatch           5   8: 0-7             0:
      yana15 pbatch           1   8: 0-7             0:
      yana16 pbatch           1   8: 0-7             0:
      yana17 pbatch           1   8: 0-7             0:
      yana18 pbatch           1   1: 7               7: 0-6
      yana19 pbatch           4   7: 0-3,5-7         1: 4
      yana20 pbatch           3   8: 0-7             0:
```

```
grondo@yana33 ~ >slurm-cpuset-info 
    JOBID USER         NCPUS  CPUS
   408792 joe              4  0-3
   410578 sarah            1  5
   393580 juan             1  7
```

---
