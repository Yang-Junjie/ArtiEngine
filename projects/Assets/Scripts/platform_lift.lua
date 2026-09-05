-- 一个上下往复的运动学平台。
--
-- 它演示的是这一轮桥接里最关键的那件事：**Kinematic 体是「目标 → 速度」驱动的，不是每步传送。**
-- 传送不产生速度，求解器看不到「这东西正在往上走」，于是平台从箱子里穿过去而不是把它托起来。
-- 现在站上来的东西（包括挂着 physics_move.lua 的玩家）会跟着一起走。
--
-- Kinematic 的 transform 归场景，所以这里直接写 entity.translation —— 那就是下一个固定步的目标。
-- 写在 on_fixed_update 而不是 on_update 里：物理每个固定步消费一次目标，按渲染帧写会让平台的
-- 速度跟着帧率抖。

local RANGE = 1.5
local SPEED = 1.2

local base = nil
local phase = 0.0

function on_create(entity)
    local t = entity.translation
    base = { x = t.x, y = t.y, z = t.z }
end

function on_fixed_update(entity, fixed_dt)
    if not base then
        return
    end
    phase = phase + SPEED * fixed_dt
    local t = entity.translation
    t.x = base.x
    t.y = base.y + RANGE * math.sin(phase)
    t.z = base.z
    entity.translation = t
end
