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
# iorelay-bind-nfs - bind directories from mntpt over all nfs mounted 
#                    file systems
#
# Run as root in private namespace
#
declare -r prog=iorelay-bind-nfs

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
    echo "Usage: $prog -m mntpt"
    exit 1
}
listnfs ()
{
    local src dst typ opts a1 a2

    cat /proc/mounts | while read src dst typ opts a1 a2; do
        [ ${typ} = nfs ] && echo ${dst}
        fi
    done
}

[ -n "$SLURM_NODELIST" ] || die "SLURM_NODELIST is not set"
relayhost=$(echo $SLURM_NODELIST | glob-hosts -n1)
[ "$(hostname)" = "$relayhost" ] && exit 0 # silently exit if relayhost

uopt=0
mntpt=""
while getopts "m:" opt; do
    case ${opt} in
        m) mntpt=${OPTARG} ;;
        *) usage ;;
    esac
done
shift $((${OPTIND} - 1))
[ $# = 0 ] || usage
[ -n "$mntpt" ] || usage
[ -d $mntpt ] || die "not a directory: $mntpt"

count=0
for dir in $(listnfs); do
    if [ -d ${mntpt}/${dir} ]; then
        mount --bind ${mntpt}/${dir} ${dir} || warn "bind ${dir} failed"
        count=$(($count+1))
    fi
done
warn "relayed $count file systems"

exit 0
