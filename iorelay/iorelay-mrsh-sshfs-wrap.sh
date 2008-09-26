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
# sshfs-mrsh-wrap - wrapper for mrsh for sshfs usage
#
declare -r prog=iorelay-sshfs-mrsh-wrap

die () {
    echo "$prog: $1" >&2
    exit 1
}

# Expected args: 
#   -x -a -oClearAllForwardings=yes -2 user@host -s sftp
# We ignore everything except user@host arg
for arg in $*; do
    if echo $arg | grep -q "@"; then
        user=$(echo $arg | cut -d@ -f1)
        host=$(echo $arg | cut -d@ -f2)
    fi
done

[ -n "$user" ] && [ -n "$host" ] || die "no user@host arg"

exec /usr/bin/mrsh -l $user $host /usr/libexec/openssh/sftp-server
die "failed to exec mrsh"
# NOTREACHED
