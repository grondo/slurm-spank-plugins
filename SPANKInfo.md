## SPANK - SLURM Plug-in Architecture for Node and job (K)control ##

### Introduction ###
_from the spank(8) man page_

The SPANK framework in SLURM provides a very generic interface for
stackable, dynamically loaded plug-ins, that may be used to modify
the job launch code in SLURM. SPANK plugins may  be  built  without
access to SLURM source code. They need only be compiled against
SLURM's spank.h  header  file, added  to  the SPANK  config  file
plugstack.conf, and they will be loaded at runtime during the next
job launch. Thus, the  SPANK  infrastructure  provides administrators
and  other developers a low cost, low effort ability to dynamically
modify the runtime behavior of SLURM job launch.

SPANK plugins also have the ability to dynamically add new user
options to srun. These options will show up in the srun --help
output in a section entitled `Options provided by plugins:`.
For example, with several plugins enabled, this section of our
srun --help output appears as:
```
Options provided by plugins:
      --renice=[prio]         Re-nice job tasks to priority [prio].
      --use-env=[name]        Read env from ~/.slurm/environment/[name] or
                              /etc/slurm/environment/[name]
      --root=[name]           Run in alternate root [name]
      --io-watchdog=[args..]  Use io-watchdog service for this job (args=`help'
                              for more info)
      --auto-affinity=[args]  Automatic, best guess CPU affinity for SMP
                              machines (args=`help' for more info)
      --overcommit-memory=[m] Choose memory overcommit mode [m] (always|off|on)
                              for all nodes of job.
      --system-safe           Replace system(3) with version safe for MPI.
      --no-system-safe        Disable system(3) replacement.
```

### Enabling SPANK Plugins ###

SPANK plugins are enabled by adding them to the **plugstack.conf** config
file. This config file contains a list of plugins to load, and any
options to pass to these plugins, the format is very similar to PAM
stack config files -- the format of each line is:
```
  required/optional    plugin    args...
```

Additionally an **include** keyword is supported, which includes
another set of config files with a pathname glob, e.g.
```
  include plugstack.d/*
```

will include all files in the plugstack.d directory (relative to
the path of plugstack.conf).

Plugins are called in order from the plugstack.conf. An example
plugstack.conf might look like:
```
#
# SLURM plugin stack config file
#
# required/optional    plugin                arguments
#
  required             oom-detect.so         do_syslog
  required             renice.so             min_prio=-20 default=0
  required             use-env.so            default=mvapich
  required             chroot.so
  required             io-watchdog.so
  required             auto-affinity.so
  required             overcommit-memory.so  ratio=90
  optional             system-safe.so
```