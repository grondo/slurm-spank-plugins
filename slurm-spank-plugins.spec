##
# $Id: chaos-spankings.spec 7813 2008-09-25 23:08:25Z grondo $
##

#
#  Allow defining --with and --without build options or %_with and %without in .
#    _with    builds option by default unless --without is specified
#    _without builds option iff --with specified
#
%define _with_opt() %{expand:%%{!?_without_%{1}:%%global _with_%{1} 1}}
%define _without_opt() %{expand:%%{?_with_%{1}:%%global _with_%{1} 1}}

#
#  _with helper macro to test for slurm_with_*
#
%define _with() %{expand:%%{?_with_%{1}:1}%%{!?_with_%{1}:0}}

#
#  Build llnl plugins and cpuset by default on chaos systems
#

%if 0%{?chaos}
%_with_opt llnl_plugins
%_with_opt lua
%else
%_without_opt llnl_plugins
%_without_opt cpuset
%_without_opt lua
%endif

%if %{?chaos}0 && 0%{?chaos} < 5
%_with_opt sgijob
%_with_opt cpuset
%else
%_without_opt sgijob
%_without_opt cpuset
%endif



Name:    
Version:
Release:    

Summary:    SLURM SPANK modules for CHAOS systems
Group:      System Environment/Base
License:    GPL

BuildRoot:  %{_tmppath}/%{name}-%{version}
Source0:    %{name}-%{version}.tgz
Requires: slurm
Obsoletes: chaos-spankings

BuildRequires: slurm-devel bison flex

%if %{_with cpuset}
BuildRequires: libbitmask libcpuset
BuildRequires: pam-devel
%endif

%if %{_with sgijob}
BuildRequires: job
%endif

%if %{_with lua}
BuildRequires: lua-devel >= 5.1
%endif


%description
This package contains a set of SLURM spank plugins which enhance and
extend SLURM functionality for users and administrators.

Currently includes:
 - renice.so :      add --renice option to srun allowing users to set priority 
                    of job
 - system-safe.so : Implement pre-forked system(3) replacement in case MPI
                    implementation doesn't support fork(2).
 - iotrace.so :     Enable tracing of IO calls through LD_PRELOAD trick
 - use-env.so :     Add --use-env flag to srun to override environment
                    variables for job
 - auto-affinity.so: 
                    Try to set CPU affinity on jobs using some kind of 
                    presumably sane defaults. Also adds an --auto-affinity
                    option for tweaking the default behavior.

 - overcommit-memory.so : 
                    Allow users to choose overcommit mode on nodes of
                    their job.

 - pty.so :         Run task 0 of SLURM job under pseudo tty.
 - preserve-env.so: Attempt to preserve exactly the SLURM_* environment
                    variables in remote tasks. Meant to be used like:
		     salloc -n100 srun --preserve-slurm-env -n1 -N1 --pty bash
 - setsched.so :    enable administrators to enforce a particular kernel scheduling
                    policy for tasks spawned by slurm

%if %{_with llnl_plugins}
%package  llnl
Summary:  SLURM spank plugins LLNL-only
Group:    System Environment/Base
Requires: slurm
%if %{_with sgijob}
Requires: job
%endif
Obsoletes: chaos-spankings

%description llnl
The set of SLURM SPANK plugins that will only run on LLNL systems.
Includes:
 - private-mount.so :
                   Run jobs or tasks in a private file system namespace
                   and privately mount file systems from /etc/slurm/fstab.
%endif


%if %{_with cpuset}
%package  cpuset
Summary:  Cpuset spank plugin for slurm.
Group:    System Environment/Base
Requires: libbitmask libcpuset slurm pam
Obsoletes: chaos-spankings-cpuset

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

%endif

%if %{_with lua}
%package lua
Summary: lua spank plugin for slurm.
Group:   System Environment/Base
Requires:lua >= 5.1

%description lua
The  lua.so spank plugin for SLURM allows lua scripts to take the
place of compiled C shared objects in the SLURM spank(8) framework.
All  the power  of  the  C  SPANK  API is exported to lua via this
plugin, which loads one or scripts and executes lua functions during
the  appropriate SLURM phase (as described in the spank(8) manpage).

%endif

%prep
%setup

%build
make \
  %{?_with_llnl_plugins:BUILD_LLNL_ONLY=1} \
  %{?_with_cpuset:BUILD_CPUSET=1} \
  %{?_with_lua:WITH_LUA=1} \
  %{?chaos:HAVE_SPANK_OPTION_GETOPT=1} \
  CFLAGS="$RPM_OPT_FLAGS" 

%if %{_with lua}
  cd lua && make check
%endif

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"

make \
  LIBNAME=%{_lib} \
  LIBDIR=%{_libdir} \
  BINDIR=%{_bindir} \
  SBINDIR=/sbin \
  LIBEXECDIR=%{_libexecdir} \
  DESTDIR="$RPM_BUILD_ROOT" \
  %{?_with_llnl_plugins:BUILD_LLNL_ONLY=1} \
  %{?_with_cpuset:BUILD_CPUSET=1} \
  %{?_with_lua:WITH_LUA=1} \
  install

%if %{_with cpuset}
# slurm-cpuset init script
install -D -m0755 cpuset/cpuset.init \
		$RPM_BUILD_ROOT/%{_sysconfdir}/init.d/slurm-cpuset
%endif

# create /etc/slurm/plugstack.d directory
mkdir -p $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d

#
#  As of SLURM 1.4.x, preserve-env functionality is availble 
#   directly in SLURM. We keep the plugin around for reference,
#   but do not install it.
#
# create entry for preserve-env.so
#echo " required  preserve-env.so" > \
#     $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d/99-preserve-env
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/preserve-env.so

%if %{_with lua}
echo " required  lua.so /etc/slurm/lua.d/*.lua" > \
     $RPM_BUILD_ROOT/%{_sysconfdir}/slurm/plugstack.conf.d/99-lua
install -D -m0644 lua/spank-lua.8 $RPM_BUILD_ROOT/%{_mandir}/man8/spank-lua.8
%endif

%clean
rm -rf "$RPM_BUILD_ROOT"

%if %{_with cpuset}
%post cpuset
if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --add slurm-cpuset; fi

%preun cpuset
if [ "$1" = 0 ]; then
  if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --del slurm-cpuset; fi
fi
%endif

%files 
%defattr(-,root,root,0755)
%doc NEWS NEWS.old ChangeLog README.use-env
%{_libdir}/slurm/renice.so
%{_libdir}/slurm/system-safe.so
%{_libdir}/slurm/iotrace.so
%{_libdir}/slurm/tmpdir.so
%{_libdir}/slurm/use-env.so
%{_libdir}/slurm/overcommit-memory.so
%{_libdir}/slurm/auto-affinity.so
%{_libdir}/slurm/pty.so 
%{_libdir}/slurm/addr-no-randomize.so
%{_libdir}/system-safe-preload.so
%{_libexecdir}/%{name}/overcommit-util
%{_libdir}/slurm/setsched.so
%dir %attr(0755,root,root) %{_sysconfdir}/slurm/plugstack.conf.d

%if %{_with llnl_plugins}
%files llnl
%defattr(-,root,root,0755)
%doc NEWS NEWS.old ChangeLog
%{_libdir}/slurm/private-mount.so
%endif

%if %{_with cpuset}
%files cpuset
%defattr(-,root,root,0755)
%doc NEWS NEWS.old ChangeLog cpuset/README
%{_sysconfdir}/init.d/slurm-cpuset
%{_libdir}/slurm/cpuset.so
/%{_lib}/security/pam_slurm_cpuset.so
/sbin/cpuset_release_agent
%{_mandir}/man1/use-cpusets.*
%{_mandir}/man8/pam_slurm_cpuset.*
%{_mandir}/man8/slurm-cpuset.*
%endif


%if %{_with lua}
%files lua
%{_sysconfdir}/slurm/plugstack.conf.d/99-lua
%{_libdir}/slurm/lua.so
%{_mandir}/man8/spank-lua*
%{_libdir}/lua/5.1/*
%endif

