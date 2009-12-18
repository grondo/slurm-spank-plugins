-- ==========================================================================
--
--  Add a --hugepages option to s{run,queue,batch} to allow users
--   to configure hugetlbfs for the nodes of their job.
--
-- ==========================================================================
local hugepages

-- Export new --hugepages option to SLURM:
spank_options = {
	{
	name =    "hugepages",
	usage =   "Attempt to create N (kB,MB,GB) worth of HugePages"..
		      " on the nodes of of the job.",
	arginfo = "N[KMG]",
	has_arg = 1,
	cb =      "opt_handler"
	},
}


-- Validate that the suffix of the hugepages option argument is valid:
function valid_suffix (suffix)
	local valid_suffixes = { 'K', 'M', 'G', 'B' }

	-- No suffix == bytes and is acceptable
	if suffix == nil then return true end

	for _,s in ipairs (valid_suffixes) do
		if suffix:upper() == s then return true end
	end
	return false
end

-- Validate the option argument to --hugepages.
function validate_hugepages (arg)
	local n = arg:match ("^[%d]+")

	if tonumber(n) <= 0 then
		SPANK.log_error ("invalid --hugepages value '%d'\n", n)
		return false
	end

	local suffix = arg:match ("[^%d]+$")
	if not valid_suffix (suffix) then
		SPANK.log_error ("invalid --hugepages suffix '%s'\n", suffix)
		return false
	end

	return true
end

-- Option handler:
function opt_handler (val, optarg, isremote)
	hugepages = optarg
	if isremote or validate_hugepages (optarg) then
		return SPANK.SUCCESS
	end
	return SPANK.FAILURE
end

function slurm_spank_init_post_opt (spank)
	--
	-- Do nothing in remote context or when no --hugepages option was seen
	--
	if spank.context == "remote" or hugepages == nil then
		return SPANK.SUCCESS
	end

	-- Export SPANK_HUGEPAGES to SLURM prolog/epilog:
	local rc, msg = spank:job_control_setenv ("HUGEPAGES", hugepages, 1);
	if rc == nil then
		return SPANK.log_error ("Unable to propagate HUGEPAGES=%s: %s",
						hugepages, msg)
	end
	return SPANK.SUCCESS
end
