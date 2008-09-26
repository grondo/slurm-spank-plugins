#!/bin/bash
###############################################################################
#
#  Copyright (C) 2007-2008 Lawrence Livermore National Security, LLC.
#  Produced at Lawrence Livermore National Laboratory.
#  Written by Jim Garlick <garlick@llnl.gov>.
#
#  UCRL-CODE-235358
# 
#  This file is part of chaos-spankings, a set of spank plugins for SLURM.
# 
#  This is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
#  for more details.
# 
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 
###############################################################################
#
# iorelay-mount-nodezero - mount / from first slurm node on /mnt
#
# Run as root in private namespace.
#
declare -r prog=iorelay-mount-nodezero
declare -r sshcmd=/usr/libexec/iorelay-mrsh-sshfs-wrap

die () 
{
    echo "$prog: $1" >&2
    exit 1
}
warn () 
{
    echo "$prog: $1" >&2
}
usage ()
{
    echo "Usage: $prog -m mntpt -u username"
    exit 1
}


[ -n "$SLURM_NODELIST" ] || die "SLURM_NODELIST is not set"
relayhost=$(echo $SLURM_NODELIST | glob-hosts -n1)
[ -n "$relayhost" ] || die "could not determine relayhost"
[ "$(hostname)" = "$relayhost" ] && exit 0 # silently exit if relayhost

mntpt=""
username=""
while getopts "u:m:" opt; do
    case ${opt} in
        m) mntpt=${OPTARG} ;;
        u) username=${OPTARG} ;;
        *) usage ;;
    esac
done
shift $((${OPTIND} - 1))
[ $# = 0 ] || usage
[ -n "$mntpt" ] || usage
[ -d $mntpt ] || die "not a directory: $mntpt"
[ -n "$username" ] || usage
uid=$(id -u $username 2>&1) || die "no such user: $username"
[ "$uid" != 0 ] || die "sshfs as root is unsupported"

grep -q sshfs /proc/mounts && die "sshfs is already mounted"

# NOTE: work around missing -n option in sshfs/fusermount 
mv -f /etc/mtab /etc/mtab-iorelay || die "failed to back up /etc/mtab"
sshfs -o ssh_command=${sshcmd} ${username}@${relayhost}/ ${mntpt}
result=$?
mv -f /etc/mtab-iorelay /etc/mtab || warn "failed to restore /etc/mtab"
[ $result = 0 ] || die "sshfs mount ${username}@${relayhost}/ ${mntpt} failed"

exit 0
