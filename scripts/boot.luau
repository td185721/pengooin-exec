-- boot.luau — runs once immediately after env_install completes.
-- wires up the tick pumps for WebSocket + Drawing, sets up r9k.* facade,
-- and re-exposes the sUNC/UNC surface under both bare-global and identify.
--
-- executed via r9k::luau::run(read_file(scripts/boot.luau)) from
-- environment.cpp's install() as its final step.
local RunService = game:GetService("RunService")

-- Drawing render tick — walks live objects once per client frame
RunService.RenderStepped:Connect(function()
    if _drawing_render then _drawing_render() end
end)

-- WebSocket pump — every Heartbeat, drain any received frames onto
-- OnMessage callbacks installed on ws objects
RunService.Heartbeat:Connect(function()
    if _pump_ws then _pump_ws() end
end)

-- identify — every executor exposes this pair for hub scripts to gate on
function identifyexecutor()
    return "pengooin", "1.0.0"
end
function getexecutorname() return "pengooin" end
function get_executor_name() return "pengooin" end

-- HttpService bypass — game:HttpGet / HttpPost wrappers land through httpget
-- so scripts written against pre-2020 Roblox APIs work unchanged
local origHttpGet  = game.HttpGet
local origHttpPost = game.HttpPost
game.HttpGet  = function(_, url, nocache) return httpget(url, nocache) end
game.HttpPost = function(_, url, body, ct) return httppost(url, body, ct) end

-- print a landing banner so users know the executor is live
print("[pengooin] runtime online — sUNC surface installed")
