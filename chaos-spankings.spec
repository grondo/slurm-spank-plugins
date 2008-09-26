##
# $Id: chaos-spankings.spec 7813 2008-09-25 23:08:25Z grondo $
##

Name:    
Version:
Release:    

Summary:    SLURM SPANK modules for CHAOS systems
Group:      System Environment/Base
License:    GPL

BuildRoot:  %{_tmppath}/%{name}-%{version}
Source0:    %{name}-%{version}.tgz

BuildRequires: slurm-devel job bison flex
BuildRequires: libbitmask libcpuset
BuildRequires: pam-devel

Requires: slurm

%description
This package contains a set of SLURM SPANK modules for CHAOS clusters.
Currently includes:
 - renice.so :      add --renice option to srun allowing users to set priority 
                    of job
 - oom-detect.so :  Detect tasks killed by OOM killer via /proc/oomkilled file.
 - system-safe.so : Implement pre-forked system(3) replacement in case MPI
                    implementation doesn't support fork(2).
 - iotrace.so :     Enable tracing of IO calls through LD_PRELOAD trick
 - use-env.so :     Add --use-env flag to srun to override environment
                    variables for job
 - tmpdir.so  :     Create a job-specific TMPDIR and remove it (as the user)
                    after the job has exited.

 - auto-affinity.so: 
                    Try to set CPU affinity on jobs using some kind of 
                    presumably sane defaults. Also adds an --auto-affinity
                    option for tweaking the default behavior.

 - overcommit-memory.so : 
                    Allow users to choose overcommit mode on nodes of
                    their job.

 - pty.so :         Run task 0 of SLURM job under pseudo tty.

%package cpuset
Summary: Cpuset spank plugin for slurm.
Group:      System Environment/Base
Requires: libbitmask libcpuset slurm

%description cpuset
This package contains a SLURM spank plugin for enabling
the use of cpusets to constrain CPU use of jobs on nodes to
the number of CPUs allocated. This plugin is specifically
designed for systems sharing nodes and using CPU scheduling
(i.e.  using the sched/cons_res plugin). Most importantly the
plugin will be harmful when overallocating CPUs on nodes. The
plugin is enabled by adding the line:

 required cpuset.so [options]

to /etc/slurm/plugstack.conf.

A PAM module - pam_slurm_cpuset.so - is also provided for
constraining user logins in a similar fashion. For more
information see the slurm-cpuset(8) man page provided with
this package.


%prep
%setup

%build
make CFLAGS="$RPM_OPT_FLAGS"

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"

plugins="renice.so \
         oom-detect.so \
         system-safe.so \
         iotrace.so \
         tmpdir.so \
         use-env/use-env.so \
         overcommit-memory/overcommit-memory.so \
         auto-affinity.so \
         preserve-env.so \
         pty.so 
        "

libs="system-safe-preload.so"
utilities="overcommit-memory/overcommit-util"

libdir=$RPM_BUILD_ROOT%{_libdir}
plugindir=${libdir}/slurm
utildir=$RPM_BUILD_ROOT%{_libexecdir}/chaos-spankings/

mkdir -p --mode=0755 $plugindir
mkdir -p --mode=0755 $utildir

cat /dev/null > std-plugins.list
for plugin in $plugins; do
   install -m0755 $plugin $plugindir
   echo %{_libdir}/slurm/$(basename $plugin) >>std-plugins.list
done

for lib in $libs; do
   install -m0755 $lib $libdir
done
      
for utility in $utilities; do
   install -m0755 $utility $utildir
done

#
# cpuset_release_agent goes into /sbin
#
mkdir -p $RPM_BUILD_ROOT/sbin
install -m0755 cpuset/cpuset_release_agent $RPM_BUILD_ROOT/sbin
install -m0755 cpuset/cpuset.so $plugindir
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/init.d/
install -m0755 cpuset/cpuset.init \
               $RPM_BUILD_ROOT/%{_sysconfdir}/init.d/slurm-cpuset

mkdir -p $RPM_BUILD_ROOT/%{_mandir}/man1
mkdir -p $RPM_BUILD_ROOT/%{_mandir}/man8
mkdir -p $RPM_BUILD_ROOT/%{_lib}/security

install -m0755 cpuset/pam_slurm_cpuset.so $RPM_BUILD_ROOT/%{_lib}/security
install -m0644 cpuset/slurm-cpuset.8 cpuset/pam_slurm_cpuset.8 \
	       $RPM_BUILD_ROOT/%{_mandir}/man8
install -m0644 cpuset/use-cpusets.1 \
	       $RPM_BUILD_ROOT/%{_mandir}/man1

# create /etc/slurm/plugstack.d directory
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d

# create entry for preserve-env.so
echo " required  preserve-env.so" > \
     $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d/99-preserve-env

%clean
rm -rf "$RPM_BUILD_ROOT"

%post cpuset
if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --add slurm-cpuset; fi

%preun cpuset
if [ "$1" = 0 ]; then
  if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --del slurm-cpuset; fi
fi

%files -f std-plugins.list 
%defattr(-,root,root,0755)
%doc NEWS ChangeLog README.use-env
/%{_libdir}/*.so
/%{_libexecdir}/chaos-spankings/*
%dir %attr(0755,root,root) %{_sysconfdir}/slurm/plugstack.conf.d
%config(noreplace) %{_sysconfdir}/slurm/plugstack.conf.d/*

%files cpuset
%defattr(-,root,root,0755)
%doc NEWS ChangeLog cpuset/README
%{_sysconfdir}/init.d/slurm-cpuset
%{_libdir}/slurm/cpuset.so
/%{_lib}/security/pam_slurm_cpuset.so
/sbin/cpuset_release_agent
%{_mandir}/man1/use-cpusets.*
%{_mandir}/man8/pam_slurm_cpuset.*
%{_mandir}/man8/slurm-cpuset.*


