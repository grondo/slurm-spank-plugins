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


#ifndef _CPUSET_LOG_H
#define _CPUSET_LOG_H

#define C_LOG_QUIET  -2
#define C_LOG_CRIT   -1
#define C_LOG_NORMAL  0
#define C_LOG_VERBOSE 1
#define C_LOG_DEBUG   2
#define C_LOG_DEBUG2  3

typedef int (log_f) (const char *msg);

int log_add_dest (int level, log_f *fn);
int log_update (int level, log_f *fn);
int log_set_prefix (const char *prefix);
void log_cleanup ();
int log_err (const char *format, ...);
void log_msg (const char *format, ...);
void log_verbose (const char *format, ...);
void log_debug (const char *format, ...);
void log_debug2 (const char *format, ...);

/*
 *  Legacy logging functions
 */
#define cpuset_error(args...)   log_err (args)
#define cpuset_verbose(args...) log_verbose (args)
#define cpuset_debug(args...)   log_debug (args)
#define cpuset_debug2(args...)  log_debug2 (args)

#endif
