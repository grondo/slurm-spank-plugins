### DESCRIPTION ###

The **setsched** plugin for SLURM can be used by administrators to configure a particular kernel scheduling policy for SLURM jobs.

### REQUIREMENTS ###

This plugin requires to use **POSIX** systems supporting sched\_setscheduler(), i.e. defining _**POSIX\_PRIORITY\_SCHEDULING** in <unistd.h>._

### CONFIGURATION ###

#### slurmd side ####

The following configuration parameters can be used in the plugstack configuration file :

| **Parameter**                  | **Description**                                  |
|:-------------------------------|:-------------------------------------------------|
| **policy** = _POL_             | set the kernel scheduling policy to the integer value _POL_ (default is 0, for SCHED\_OTHER) |
| **priority** = _PRIO_          | set the associated priority to the integer value _PRIO_ (default is 0) |
| **default** = _MODE_           | set the default behavior of the plugin, valid values being _enabled_ and _disabled_. Note that regardless of the behavior, users will still have the availability to activate/inhibate the use using --setsched srun option |

#### client side ####

As long as setsched is configured in plugstack, users can choose to use it or not as well as use the default mode set by the administrator. Three values can be used with **--setsched** for that purpose :
  * yes : to force the use of the configured scheduling strategy
  * no : to ensure that the configured scheduling strategy will no be used
  * auto : to follow the administrator choice

The default is **auto**.

#### Example ####

```
[bob@foo1 ~]$ tail -n 1 /etc/slurm/plugstack.conf.d/setsched.conf
optional /tmp/setsched.so policy=55 priority=0 default=disabled
[bob@foo1 ~]$ srun -w foo1 --setsched=yes cat /proc/self/sched | egrep "policy"
policy                             :                   55
[bob@foo1 ~]$ srun -w foo1 --setsched=no cat /proc/self/sched | egrep "policy"
policy                             :                    0
[bob@foo1 ~]$
```