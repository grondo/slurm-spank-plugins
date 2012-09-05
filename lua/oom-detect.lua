--[==========================================================================
 *
 *  Copyright (C) 2007-2012 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *
 *  UCRL-CODE-235358
 *
 *  This file is part of slurm-spank-plugins,
 *    a set of spank plugins for SLURM.
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
-==========================================================================--]
--
--  Poor man's OOM detection plugin for SLURM.
--  
--   For each exiting task, search dmesg output for a message from
--    the OOM killer matching the exiting task's PID. If found, then
--    print an error on job's stderr and kill remaining job step tasks.
--
--   The pattern in check_oom_kill() may need to be updated for
--    different kernels. The original version in this file was for
--    RHEL6.2.
--

local posix = require 'posix'

--- Log an error with SLURM's log facility
local function log_err (...)
    SPANK.log_error (...)
end

--- Log an informational message with SLURM's log facility
local function log_info (...)
    local msg = string.format (...)
    SPANK.log_info ("oom-detect: %s", msg)
end

--- Get a spank item `item' from spank handle `spank' with name `name'
local function get_item (spank, item, name)
    local v, err = spank:get_item (item)
    if not v then
        log_err ("Unable to get %s: %s", name, err)
        return nil
    end
    return v
end

--- Create a table of job info from the spank handle `spank'
-- @spank valid spank context for a slurm job
-- Returns a job table with the following entries:
-- job
--  .jobid     SLURM job id
--  .stepid    SLURM job step id
--  .uid       user id
--  .ntasks    number of local tasks
--  .task      Table of task info
--  .task.id   Current task id
--   .pid      Current task pid
--   .status   Current task exit status
--   .exitcode Current task exit code
--   .signal   If task killed by signal, then signo
--   .coredump true if task dumped core at exit
--
local function job_table_create (spank)
    local entries = {
        jobid =  "S_JOB_ID",
        stepid = "S_JOB_STEPID",
        uid =    "S_JOB_UID",
        taskid = "S_TASK_ID",
        ntasks = "S_JOB_LOCAL_TASK_COUNT",
        pid =    "S_TASK_PID",
    }
    local job = {}

    for name,item in pairs (entries) do
        job[name] = get_item (spank, item, name)
        if not job[name] then
            return nil
        end
    end
    job.task = {
    	id = tonumber(job.taskid),
        pid = tonumber(job.pid),
    }
    job.task.status, job.task.exitcode, job.task.signal, job.task.coredump =
        spank:get_item ("S_TASK_EXIT_STATUS")

    return job
end


--- Check for OOM kill information from dmesg output `line'
function check_oom_kill (line)
    local p = "Killed process (%d+), UID %d+, %((.+)%).*:(%d+)kB.*:(%d+)kB.*:(%d+)kB"
    return string.match (line, p);
end

--- Log OOM killed task info via syslog
-- @job  job table for current job
-- @comm short command name for killed task
-- @mb   table of vm stats in MB
--
function syslog_oom_kill (job, comm, mb)
    local fmt = "OOM detected: " ..
                "jobid=%u.%u uid=%u taskid=%d ntasks=%d comm=%s " ..
                "vsz=%.1fM rss=%.1fM"
    local msg = string.format (fmt, job.jobid, job.stepid,
                               job.uid, job.task.id, job.ntasks,
                               comm, mb.vsz, mb.rss)
    posix.openlog ("slurmd")
    posix.syslog (posix.LOG_WARNING, msg)
    posix.closelog()

end

--- Log OOM kill info for job `job'
-- @job       job table for current job
-- @comm      short command name for killed task
-- @vsz       virtual address space size in Kb
-- @anon_rss  Anonymous RSS in kb
-- @file_rss  File RSS in kb
--
function log_oom_kill (job, comm, vsz, anon_rss, file_rss)
    local msg = "task%d: [%s] invoked OOM killer: vsz=%.1fM rss=%.1fM"
    local mb = { vsz = vsz / 1024,
                 rss = (anon_rss + file_rss) / 1024 }
    log_err (msg, job.taskid, comm, mb.vsz, mb.rss)
    syslog_oom_kill (job, comm, mb);
end

---
---  Kill remaining tasks in this job step after an OOM event:
-- @job job table for current job
--
function kill_all_step_tasks (job)
    local cmd = string.format ("scontrol listpids %d.%d", job.jobid, job.stepid)

    local f, err = io.popen (cmd)
    if f == nil then
        log ("%s: %s", cmd, err)
	return
    end

    local n = 0
    for line in f:lines () do
        local pid = string.match (line, "(%d+) .+")
	if pid then
	    posix.kill (pid, 9)
	    n = n + 1
	end
    end

    f:close()
end

--- Plugin hook called for each task exit event in the current job step
--
-- Check eack task exit to see if it was killed by the OOM killer, and print
--  a message to stderr and syslog if so.
--
function slurm_spank_task_exit (spank)

    local job = job_table_create (spank)

    --  If this task has been terminated by OOM killer, then exit
    --   status will be '9' (Killed). Otherwise, don't bother
    --   searching dmesg output.
    if job.task.signal ~= 9 then
        return SPANK.SUCCESS
    end

    local f, err = io.popen ("/bin/dmesg")
    if f == nil then
        log ("/bin/dmesg: %s", err)
        return SPANK.FAILURE
    end

    for line in f:lines () do
        local pid, comm, vsz, rss, file_rss = check_oom_kill (line)
        if job.task.pid == tonumber (pid) then
            log_oom_kill (job, comm, vsz, rss, file_rss)
            kill_all_step_tasks (job)
            break
        end
    end

    f:close()

    return SPANK.SUCCESS
end
