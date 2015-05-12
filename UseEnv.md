
---

### Flexible job environment customization with the use-env.so plugin ###

#### SYNOPSIS ####
The **use-env** `spank(8)` plugin for SLURM provides a simple facility for
utilizing SLURM to initialize and/or modify the current environment for
users launching jobs through `srun(1)`.  When the plugin is enabled in the
spank plugin stack (`plugstack.conf` by default), it reads environment
overrides from a default config file at srun initialization, and
also allows user-selected environment overrides via the srun option
`--use-env=name`.

When using --use-env=name, the config file
loaded is from `~/.slurm/environment/name` or `/etc/slurm/environment/name`.
The format of the config file is described below.

This plugin also supports generation of a different environment per
task through the use of **in task** blocks, which are parsed by slurmd
in task context just before calling exec(). See the
[TASK BLOCKS](UseEnv#TASK_BLOCKS.md) section in the UseEnvSyntax page
for more information.

#### DEFAULT CONFIG ####

The default config file is read from `/etc/slurm/environment/default`
and is always used if it exists. A user default is also read from
`~/.slurm/environment/default` if it exists. Settings in the user file are
applied after the global defaults in `/etc/slurm` so that user settings may
override system defaults. The default environment settings are applied
before any user-selected environment via the --use-env option.

The name of the global default config can be overridden by use of the
"default=" option to plugin, e.g., with the following line in
plugstack.conf:

```
  required   use-env.so  default=mvapich }}}
```

would read /etc/slurm/environment/mvapich by default instead of
/etc/slurm/environment/default. The user default file is always
named "default" however.

For a detailed description of the use-env config file syntax
See UseEnvSyntax, or the
[README.use-env](http://slurm-spank-plugins.googlecode.com/svn/trunk/README.use-env)
file in the [slurm-spank-plugins](http://slurm-spank-plugins.googlecode.com)
distribution.

#### EXAMPLES ####

The following are some short examples to give an idea of what
can be done with the use-env.so plugin.

/etc/slurm/environment/default:
```
#
# Include global defaults
include global
#
# Include environment for mvapich
include mvapich
```

/etc/slurm/environment/global:
```
#
# If TMPDIR not set, set to /tmp
TMPDIR |= /tmp
```

/etc/slurm/environment/mvapich
```
#
#   Force MVAPICH timeout to 22
#
VIADEV_DEFAULT_TIME_OUT=22
#
# Prepend /usr/lib/mpi/dbg/mvapich-gen2/lib/shared to LD_LIBRARY_PATH
LD_LIBRARY_PATH += /usr/lib/mpi/dbg/mvapich-gen2/lib/shared
```

With the following file in `~/.slurm/environment/mvapich-test`
PATH and LD\_LIBRARY\_PATH will automatically be adjusted to use
an mvapich test version with the srun command line
` srun --use-env=mvapich-test ...`
```
# environment for testing new versions of MVAPICH
#
PATH += /home/grondo/mvapich-test/root/lib/shared
LD_LIBRARY_PATH += /home/grondo/mvapich-test/root/bin
```


Using conditional expressions:

~/.slurm/environment/default
```
#
# Using different environment variables based on job size
#

define n = $SLURM_NPROCS
define N = $SLURM_NNODES

if ($N > 128 || $n > 1024)
   include large-env
else if (($N > 16) || ($n > 128))
   include medium-env
else
   include small-env
endif

if (defined $DEBUG)
   print "environment setup for $SLURM_JOBID.$SLURM_STEPID complete"
   dump keywords
   dump symbols
endif
```

Output for the above config file for a run with DEBUG set might look
like:

```
~ > DEBUG=1 srun hostname
environment setup for 4862.4 complete
use-env: default: 18: Dumping keywords
use-env: default: 18:  SLURM_STEPID = "4"
use-env: default: 18:  SLURM_JOBID = "4862"
use-env: default: 18:  SLURM_NPROCS = "16"
use-env: default: 18:  SLURM_NNODES = "2"
use-env: default: 19: Dumping symbols
use-env: default: 19:  N = "2"
use-env: default: 19:  n = "16"
```



---
