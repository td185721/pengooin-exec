-- sunc_runner.luau — sUNC/UNC self-test harness
--
-- run inside pengooin (Execute button) to enumerate every function in
-- the environment and check it against the canonical sUNC/UNC contract.
-- prints per-function pass/fail + a coverage percentage.
--
-- test groups roughly mirror the official sUNC suite structure so the same
-- numbers reported here map onto the public scoreboard.

local total, passed = 0, 0
local report = {}

local function test(name, fn)
    total = total + 1
    local ok, err = pcall(fn)
    if ok then
        passed = passed + 1
        report[#report + 1] = "  [pass] " .. name
    else
        report[#report + 1] = "  [FAIL] " .. name .. " — " .. tostring(err)
    end
end

local function exists(name)
    return getfenv()[name] ~= nil or getgenv()[name] ~= nil
end

-- ---- closure library ----
test("hookfunction",     function() assert(type(hookfunction)     == "function") end)
test("newcclosure",      function() assert(type(newcclosure)      == "function") end)
test("hookmetamethod",   function() assert(type(hookmetamethod)   == "function") end)
test("iscclosure",       function() assert(iscclosure(print)      == true)      end)
test("islclosure",       function() assert(islclosure(function() end) == true)  end)
test("isexecutorclosure",function() assert(isexecutorclosure(newcclosure(function() end)) == true) end)
test("checkcaller",      function() assert(checkcaller() == true) end)
test("clonefunction",    function() local c = clonefunction(print); assert(type(c) == "function"); end)
test("loadstring",       function() local f = loadstring("return 42"); assert(f and f() == 42) end)

-- ---- debug / reflection ----
test("getgc",            function() assert(type(getgc()) == "table") end)
test("getreg",           function() assert(type(getreg()) == "table") end)
test("getgenv",          function() assert(type(getgenv()) == "table") end)
test("getrenv",          function() assert(type(getrenv()) == "table") end)
test("getrawmetatable",  function() assert(type(getrawmetatable) == "function") end)
test("setrawmetatable",  function() assert(type(setrawmetatable) == "function") end)
test("setreadonly",      function() assert(type(setreadonly) == "function") end)
test("isreadonly",       function() assert(type(isreadonly) == "function") end)
test("getnamecallmethod",function() assert(type(getnamecallmethod) == "function") end)

-- ---- instances / cache ----
test("cloneref",         function() local c = cloneref(game); assert(c ~= game and c.ClassName == game.ClassName) end)
test("compareinstances", function() local c = cloneref(game); assert(compareinstances(c, game) == true) end)
test("cache.invalidate", function() assert(type(cache.invalidate) == "function") end)
test("cache.iscached",   function() assert(type(cache.iscached) == "function") end)
test("getinstances",     function() assert(type(getinstances()) == "table") end)
test("getconnections",   function() assert(type(getconnections) == "function") end)
test("firesignal",       function() assert(type(firesignal) == "function") end)
test("fireclickdetector",function() assert(type(fireclickdetector) == "function") end)
test("fireproximityprompt",function() assert(type(fireproximityprompt) == "function") end)

-- ---- filesystem ----
test("writefile+readfile", function()
    writefile("_sunc_probe.txt", "hello")
    assert(readfile("_sunc_probe.txt") == "hello")
    delfile("_sunc_probe.txt")
end)
test("isfile",           function() assert(type(isfile) == "function") end)
test("isfolder",         function() assert(type(isfolder) == "function") end)
test("makefolder+delfolder", function()
    makefolder("_sunc_dir")
    assert(isfolder("_sunc_dir"))
    delfolder("_sunc_dir")
end)
test("listfiles",        function() assert(type(listfiles(".")) == "table") end)
test("loadfile",         function() assert(type(loadfile) == "function") end)
test("getcustomasset",   function() assert(type(getcustomasset) == "function") end)

-- ---- net ----
test("httpget",          function() local s = httpget("https://httpbin.org/get"); assert(#s > 10) end)
test("request",          function() local r = request({ Url = "https://httpbin.org/get" }); assert(r.Success and r.StatusCode == 200) end)
test("WebSocket.connect",function() assert(type(WebSocket.connect) == "function") end)

-- ---- crypt ----
test("crypt.generatekey",function() local k = crypt.generatekey(); assert(#k >= 40) end)
test("crypt.hash",       function() assert(#crypt.hash("hi") == 64) end)
test("crypt.base64",     function() assert(crypt.base64decode(crypt.base64encode("test")) == "test") end)
test("crypt.encrypt/decrypt", function()
    local k = crypt.generatekey()
    local ct = crypt.encrypt("plaintext", k)
    assert(crypt.decrypt(ct, k) == "plaintext")
end)

-- ---- input ----
test("mouse1click",      function() assert(type(mouse1click) == "function") end)
test("keypress",         function() assert(type(keypress) == "function") end)
test("iskeydown",        function() assert(type(iskeydown) == "function") end)

-- ---- drawing ----
test("Drawing.new",      function() local d = Drawing.new("Line"); d:Remove() end)

-- ---- identify ----
test("identifyexecutor", function() local n = identifyexecutor(); assert(n == "pengooin") end)

print(("[sUNC] %d / %d passed  (%.1f%%)"):format(passed, total, passed / total * 100))
for _, line in ipairs(report) do print(line) end
