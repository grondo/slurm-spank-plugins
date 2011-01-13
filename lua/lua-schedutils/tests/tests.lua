
require "lunit"
local sched = require "schedutils"
local cpu_set = sched.cpuset

local fmt = string.format

module ("TestCpuSet", lunit.testcase, package.seeall) 

TestCpuSet = {

	to_string = {
		{ cpus={0,1,2,3}, result="0-3",    count=4 }, 
		{ cpus={0,2,4},   result="0,2,4",  count=3 },
		{ cpus={},        result="",       count=0 },
		{ cpus={0,1},     result="0,1",    count=2 },
	},

	new = {

		-- String input:
		{ input="0-3",               output="0-3" },
		{ input="0-20:4",            output="0,4,8,12,16,20" },
		{ input="11",                output="11"  },
		{ input="0xf",               output="0-3" },
		{ input="f",                 output="0-3" },
		{ input="0f",                output="0-3" },
		{ input="0xff",              output="0-7" },
		{ input="ff",                output="0-7" },
		{ input="0,1,2,3",           output="0-3" },
		{ input="0000000f",          output="0-3" }, 
		{ input="0x0000000f",        output="0-3" }, 
		{ input="0xff00000000000000",output="56-63" }, 

		-- Bitstring input:
		{ input="00000000,00000000,00000000,00000000,00000000,00000000,"
			  .."00000000,0000ffff", output="0-15" }, 
		{ input="000000ff,00000000,00000000,00000000,00000000,00000000,"
			  .."00000000,00000000", output="224-231" }, 
		{ input="10000000,10000000,10000000,10000000,10000000,10000000,"
			  .."10000000,10000000",
			  output="28,60,92,124,156,188,220,252" }, 
		{ input="10000000,01000000,00100000,00010000,00001000,00000100,"
			  .."00000010,00000001",
			  output="0,36,72,108,144,180,216,252" }, 

	    -- Input by numbers (raw bitmask)
        { input=0xff,                output="0-7"  },
		{ input=255,                 output="0-7"  },
		{ input=0x0,                 output=""     },
	},

	eq = {
		{ s1='0-3',   s2='0-3',   r=true },
		{ s1="0xff",  s2="0-7",   r=true }, 
		{ s1="0x1",   s2="0x1",   r=true },
		{ s1="0,100", s2="0,100", r=true },
		{ s1="",      s2="",      r=true },
		{ s1="",      s2="0x0",   r=true },
		{ s1="",      s2="0x1",   r=false },
		{ s1="1-3",   s2="0",     r=false },
	},

	len = {
		{ arg="0xffff", count=16 },
		{ arg="0,1,2",  count=3  },
		{ arg="0,1000", count=2  },
		{ arg=nil,      count=0  },
	},

	add = {
		{ a="0-3", b="",     result="0-3" },
		{ a="0-3", b="0-3",   result="0-3" },
		{ a="0-3", b="1-4",   result="0-4" },
		{ a="0-3", b="4-7",   result="0-7" },
		{ a="0-3", b="7",     result="0-3,7" },
	},

	subtract = {
		{ a="0-3", b="",      result="0-3" },
		{ a="0-3", b="0-3",   result=""    },
		{ a="0-3", b="1-4",   result="0"   },
		{ a="0-3", b="4-7",   result="0-3" },
		{ a="0-3", b="1,2",   result="0,3" },
	},
}

function test_cpu_set_constants ()
	assert_number (cpu_set.SETSIZE)
	assert_true (cpu_set.SETSIZE > 0 and cpu_set.SETSIZE < 2000)
	local c = cpu_set.new()
	assert_number(c.size)
	assert_true (c.size > 0 and c.size < 2000)
end

function test_cpu_set_error()
	local x, y = cpu_set.new ("foo");
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse CPU mask or list:", y)
	local x, y = cpu_set.union ("0x1", "f-10")
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse", y)
	local x, y = cpu_set.intersect ("0x1", "f-10")
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse", y)
	-- cpuset too big
	local x, y = cpu_set.new ("1-1024");
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse", y)

	-- Using : by accident
	assert_error_match ("Table is 1st argument to new()",
			"Table is 1st arg to new()",
			function () return cpu_set:new ("0-3") end)
	-- Wrong number of args
	assert_error_match ("> 1 arg should be an error",
			"Expected < 2 arguments to new",
			function () return cpu_set.new ("0-3", "4-7") end)
	-- 
	local x,y = cpu_set.union ("foo", "0-3")
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse", y)
	--
	local x,y = cpu_set.intersect ("foo", "0-3")
	assert_nil (x)
	assert_string (y)
	assert_match ("unable to parse", y)

	--
	local x,y = cpu_set.new(0xffffffffffffffff)
	assert_nil (x, fmt ("Expected nil, got cpuset '%s'", tostring (x)))
	assert_string (y)
	assert_match ("numeric overflow", y);

	-- Invalid index
	assert_error_match ("Invalid index",
			"Invalid index 2048 to cpu_set",
			function () c = cpu_set.new(); s=c[2048] end)
	assert_error_match ("Invalid index",
			"Invalid index 2048 to cpu_set",
			function () c = cpu_set.new(); c[2048] = 1 end)
	assert_error_match ("Invalid newindex",
			"Index of cpu_set may only be set to 0 or 1",
			function () c = cpu_set.new(); c[0] = 3 end)
end

function test_new_nil_args()
	assert_userdata (cpu_set.new())
	assert_userdata (cpu_set.new(nil))
	assert_equal (0, cpu_set.new():count())

	local c = cpu_set.new()
	assert_userdata (c) 
	assert_equal (0, #c)
    assert_equal ("", tostring(c))

	local c = cpu_set.new(nil)
	assert_userdata (c) 
	assert_equal (0, #c)
    assert_equal ("", tostring(c))

	local c = cpu_set.new("")
	assert_userdata (c) 
	assert_equal (0, #c)
    assert_equal ("", tostring(c))
end

function test_to_string()
	for _,s in pairs (TestCpuSet.to_string) do
	    local c = cpu_set.new()
		assert_userdata (c)
		for _,cpu in pairs (s.cpus) do c:set(cpu) end
		assert_equal (s.result, tostring (c))
		assert_equal (s.count, #c)
	end
end

function test_cpu_set_new()
	for _,t in pairs (TestCpuSet.new) do
		local c,err = cpu_set.new(t.input)
	    assert_userdata (c, err)
	    assert_equal (t.output, tostring(c),
				fmt("cpu_set.new('%s') == %s, expected %s", t.input,
					tostring(c), t.output))
	end
end

function test_cpu_set_copy()
	for _,t in pairs (TestCpuSet.new) do
		local c,err = cpu_set.new(t.input)
		assert_userdata (c, err)
	    local copy = c:copy()
	    assert_userdata (copy,
				fmt("failed to copy cpu_set from '%s'", t.input))
	    assert_equal (t.output, tostring(copy),
				fmt("cpu_set.new('%s') == %s, expected %s", t.input,
					tostring(c), t.output))
	end
end

function test_cpu_set_eq()
	for _,t in pairs (TestCpuSet.eq) do
		local a = cpu_set.new(t.s1)
		local b = cpu_set.new(t.s2)
		assert_userdata (a)
		assert_userdata (b)
		if (t.r == true) then
			assert_true (a == b, "cpuset "..t.s1.." == "..t.s2)
		else
			assert_false (a == b, "cpuset "..t.s1.." != "..t.s2)
		end
	end
end

function test_cpu_set_len()
	for _,t in pairs (TestCpuSet.len) do
		local c = cpu_set.new(t.arg)
		assert_userdata (c)

		local msg = string.format("c=%s: count(c) == %d (got %d)", 
				tostring(c), t.count, #c)
		assert_true (t.count == #c, msg)

		local msg = string.format("c=%s: count(c) == %d (got %d)", 
				tostring(c), t.count, c:count())
		assert_true (t.count == c:count(), msg)
	end
end

function test_cpu_set_add()
	for _,t in pairs (TestCpuSet.add) do
		local a = cpu_set.new(t.a)
		local b = cpu_set.new(t.b)
		assert_userdata (a)
		assert_userdata (b)
		local msg = string.format ("a(%s) + b(%s) == %s (got %s)",
				tostring(a), tostring(b), t.result, tostring(c))
		local c = a + b
		assert_userdata (c)
		assert_true (tostring(c) == t.result, msg) 
	end
end

function test_cpu_set_subtract()
	for _,t in pairs (TestCpuSet.subtract) do
		local a = cpu_set.new(t.a)
		local b = cpu_set.new(t.b)
		assert_userdata (a, fmt("cpu_set.new('%s') failed", t.a))
		assert_userdata (b, fmt("cpu_set.new('%s') failed", t.b))
		local msg = string.format ("a(%s) - b(%s) == %s (got %s)",
				tostring(a), tostring(b), t.result, tostring(c))
		local c = a - b
		assert_userdata (c)
		assert_true (tostring(c) == t.result, msg) 
	end

end

TestCpuSet.index = {
	{ set = nil,   args = { [0] = false, [1] = false, [10] = false } },
	{ set = "0-4", args = { [0] = true,  [1] = true,  [10] = false } },
}

function test_cpu_set_index()
	for _,t in pairs (TestCpuSet.index) do
		local c = cpu_set.new(t.set)
		assert_userdata (c)
		for i,r in pairs (t.args) do
			assert_true (c[i] == r, 
					string.format ("set %s: c[%d] is %s (expected %s)",
						tostring (c), i, tostring(c[i]), tostring (r)))
		end
	end
end

TestCpuSet.newindex = {
	{ set = nil,   args = { [0] = 1, [1] = 1, [3] = 1 }, result="0,1,3" },
	{ set = "0-3", args = { [2] = 0, }, result="0,1,3" },
	{ set = "0-3", args = { [2] = false, }, result="0,1,3" },
}

function test_cpu_set_newindex()
	for _,t in pairs (TestCpuSet.newindex) do
		local c = cpu_set.new(t.set)
		assert_userdata (c)
		for i,v in pairs (t.args) do
			c[i] = v
		end
		assert_true (tostring (c) == t.result,
				"got set " .. c .. "expected " .. t.result)
	end
end

TestCpuSet.set = {
	{ set="",  args={0,1,2,3,4,5,6,7}, result="0-7" },
	{ set="",  args={0,7},            result="0,7" },
}

function test_cpu_set_set()
	for _,t in pairs (TestCpuSet.set) do
		local c = cpu_set.new(t.set)
		assert_userdata (c)
		c:set(unpack(t.args))
		assert_true (tostring(c) == t.result,
				"got set " .. c .. "expected " .. t.result)
	end
end

TestCpuSet.clr = {
		{ set="0-3", args={0}, result="1-3" },
		{ set="0-3", args={0,3}, result="1,2" },
		{ set="0-3", args={0,1,2,3}, result="" },
}

function test_cpu_set_clr ()
	for _,t in pairs (TestCpuSet.clr) do
		local c = cpu_set.new(t.set)
		c:clr (unpack(t.args))
		assert_true (tostring(c) == t.result,
				"got set " .. c .. "expected " .. t.result)
	end
end

TestCpuSet.isset = {
	{ set="0-3", args={[0]=1,1,1,1,0,0,0}  },
	{ set="0,2,4", args={[0]=1,0,1,0,1} },
}

function test_cpu_set_isset()
	for _,t in pairs (TestCpuSet.index) do
		local c = cpu_set.new(t.set)
		assert_userdata (c)
		for i,r in pairs (t.args) do
			assert_true (c:isset(i) == r, 
					string.format ("set %s: c:isset(%d) is %s (expected %s)",
						tostring (c), i, tostring(c:isset(i)), tostring (r)))
		end
	end
end

function test_cpu_set_zero()
	local c = cpu_set.new("0-128")
	assert_userdata (c)
	assert_equal ("0-128", tostring(c))
	assert_equal (129, #c)
	c:zero()
	assert_equal (0, #c)
	assert_equal ("", tostring(c))
end

TestCpuSet.union = {
	{ a="0-3", args={"0-10"}, result="0-10" },
	{ a="",    args={"1-3"},  result="1-3" },
	{ a="0-3", args={""},     result="0-3" },
	{ a="0-3", args={"1-4"},  result="0-4" },
	{ a="0-3", args={"1","7","8"},  result="0-3,7,8" },
}

function test_cpu_set_union()
	for _,t in pairs (TestCpuSet.union) do
		local r = { cpu_set.new(t.a), cpu_set.union (t.a, unpack(t.args)) }
		assert_userdata (r[1])
		assert_userdata (r[2])
		r[1]:union(unpack(t.args))
	    for _,c in pairs(r) do
			assert_true (tostring(c) == t.result,
				string.format ("\"%s\":union(%s) == %s (got %s)",
					t.a, table.concat(t.args),t.result, tostring(c)))
	    end
	end
end

TestCpuSet.intersect = {
	{ a="0-3", args={"0-7"}, result="0-3" },
	{ a="0-3", args={"2-5"}, result="2,3" },
	{ a="0-7", args={""},    result=""    },
	{ a="0-7", args={"11"},  result=""    },
}

function test_cpu_set_intersect()
	for _,t in pairs (TestCpuSet.intersect) do
		local x = cpu_set.new(t.a)
	    local y = cpu_set.intersect(t.a, unpack(t.args))
		assert_userdata (x)
		assert_userdata (y)
		x:intersect(unpack(t.args))
		for _, c in pairs ({ x, y }) do 
		    assert_true (tostring(c) == t.result,
				string.format ("\"%s\":intersect(%s) == %s (got %s)",
					t.a, table.concat(t.args),t.result, tostring(c)))
	    end
	end
end

TestCpuSet.is_in = {
	{ a="0-3",  b="0xff",  result=true },
	{ a="0-7",  b="0xff",  result=true },
	{ a="0-15", b="0xff",  result=false},
	{ a="0-3",  b="0-3",   result=true },  -- set contains itself
	{ a="",     b="0xff",  result=true },  -- empty set is an any other set
	{ a="1",    b="",      result=false }, -- nothing in empty set
}

function test_cpu_set_is_in()
	for _,t in pairs (TestCpuSet.is_in) do
		local c = cpu_set.new(t.a)
		assert_userdata (c)
		assert_true (c:is_in(t.b) == t.result,
				string.format ("%s:is_in (%s) == %s (got %s)",
					t.a, t.b, tostring(t.result), tostring(c:is_in(t.b))))
	end
end

TestCpuSet.contains = {
	{ a="0-3",  b="0xff", result=false },
	{ a="0xff", b="0xff", result=true  },
	{ a="0-3",  b="0-3",  result=true  },
	{ a="0-3",  b="4",    result=false },
	{ a="",     b="1",    result=false },
	{ a="0-3",  b="",     result=true  },
}

function test_cpu_set_contains()
	for _,t in pairs (TestCpuSet.contains) do
		local c = cpu_set.new(t.a)
		assert_userdata (c)
		assert_true (c:contains(t.b) == t.result,
				string.format ("%s:contains (%s) == %s (got %s)",
					t.a, t.b, tostring(t.result), tostring(c:is_in(t.b))))
	end
end

function test_cpu_set_iterator()
	for _,t in pairs (TestCpuSet.new) do
		local c = cpu_set.new(t.input)
		assert_userdata (c, fmt ("cpu_set.new('%s') failed", t.input))
		local count = 0
		for i in c:iterator() do
			assert_number (i)
			count = count + 1
			assert_true (c[i],
					fmt ("iterator returned cpu%d, but c[%d] == false", i, i))
		end
		local n = #c
		assert_equal (n, count,
				fmt ("iterator got %d values for '%s', expected %d", count,
					tostring (c), c:count()))
	end
end


-- Function to return only odd numbers
local odd = function (i) return i%2 ~= 0 and i end
local endzero = function (i) return tostring(i):match("0$") and i end


TestCpuSet.expand = {
	{ input="0-7", fn=nil,      n=8, out="0,1,2,3,4,5,6,7" },
	{ input="0xf", fn=nil,      n=4, out="0,1,2,3"         },
	{ input="0xf", fn=odd,      n=2, out="1,3"             },
	{ input="",    fn=nil,      n=0, out=""                },
	{ input="500", fn=nil,      n=1, out="500"             },
	{ input="1-100",
	  fn=endzero,
	  n=10,
	  out="10,20,30,40,50,60,70,80,90,100"                 },
}

function test_cpu_set_expand()
	for _,t in pairs (TestCpuSet.expand) do
		local c = cpu_set.new(t.input)
		assert_userdata (c)
		local tbl = c:expand (t.fn)
		assert_table (tbl)
		assert_equal (t.n, #tbl,
			fmt ("%s: Expected table of size %d got %d", t.input, t.n, #tbl))
		assert_equal (t.out, table.concat (tbl, ","))
	end
end


TestCpuSet.firstlast = {
	{ input="0-7", first=0,   last=7 },
	{ input="11",  first=11,  last=11 },
	{ input="",    first=nil, last=nil },
}

function test_cpu_set_firstlast()
	for _,t in pairs (TestCpuSet.firstlast) do
		local c = cpu_set.new (t.input)
		assert_userdata (c)
		assert_equal (c:first(), t.first)
		assert_equal (c:last(), t.last)
	end
end

function test_cpu_set_tohex()
	for _,t in pairs (TestCpuSet.new) do
		local c = cpu_set.new (t.input)
		assert_userdata (c, fmt ("cpu_set.new('%s') failed", t.input))
		local hex = c:tohex()
		assert_string (hex, fmt ("c:tohex() failed: got %s", tostring (hex)))
		local new, err = cpu_set.new (hex)
		assert_userdata (new, err)
		assert_true (new == c, fmt("Error (%s)[%s] == (%s)[%s] was false",
		                           t.input, tostring(c), hex, tostring(new)))
	end
end
