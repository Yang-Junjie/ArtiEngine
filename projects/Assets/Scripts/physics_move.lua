-- WASD 推一个**动态**刚体：输入在渲染帧里采，运动在固定步里施。
--
-- 和隔壁 wasd_move.lua 的区别就在这一条。那个直接写 entity.translation，所以被推的方块必须是
-- Kinematic —— 场景拥有它的 transform，物理只读不写（Scene.md 3.1.1）。代价是它不受重力、
-- 撞上东西也只会穿过去。这个走 arti.physics.*，被推的是真的 Dynamic 刚体：会掉、会被墙挡住、
-- 能把别的箱子撞开、能站上正在升降的平台。
--
-- 挂法：选中一个带 RigidBody(Dynamic) + Collider 的实体 → Add Component → Script → 填这个
-- 脚本的 UUID → Play 或 Simulate。
--
-- 操作：WASD 走，Shift 加速，Space 跳（只在踩着东西时），R 复位回出生点。

local SPEED = 5.0
local SPRINT = 2.0
local JUMP = 5.0
-- 脚下探测的长度：半高 0.5 再多一点点余量。
local FEET = 0.62

-- 出生点，on_create 记一次，R 键回到这里。
local spawn = nil

-- on_update 采到的意图，on_fixed_update 消费。
--
-- **输入必须按渲染帧采**：一帧里可能有零个、一个或好几个固定步（追帧），把 is_key_pressed
-- 搬进固定回调就会漏帧或者把同一次按键算好几遍。反过来，施力必须在固定步里 —— 那才是
-- 物理的时钟，写在 on_update 里会跟着帧率变快变慢。
local wish_x, wish_z = 0.0, 0.0
local wants_jump = false
local wants_reset = false
local last_ground = nil

-- 脚下踩着的东西（没有就是 nil）。射线从实体中心往下打，命中自己不算。
local function ground_under(entity)
    local hit = arti.physics.raycast(entity.translation, { x = 0.0, y = -FEET, z = 0.0 })
    if hit and hit.uuid ~= entity.uuid then
        return hit
    end
    return nil
end

function on_create(entity)在·在·
    local t = entity.translation
    spawn = { x = t.x, y = t.y, z = t.z }
    arti.log.info("physics_move attached to " .. entity.name)
end

function on_update(entity, dt)
    local speed = SPEED
    if arti.input.is_key_pressed("Shift") then
        speed = speed * SPRINT
    end

    -- 屏幕空间的直觉：W 往 -Z（和相机默认朝向一致），D 往 +X。
    local dx, dz = 0.0, 0.0
    if arti.input.is_key_pressed("W") then dz = dz - 1.0 end
    if arti.input.is_key_pressed("S") then dz = dz + 1.0 end
    if arti.input.is_key_pressed("A") then dx = dx - 1.0 end
    if arti.input.is_key_pressed("D") then dx = dx + 1.0 end
    if dx ~= 0.0 or dz ~= 0.0 then
        -- 斜着走不该更快。
        local length = math.sqrt(dx * dx + dz * dz)
        dx, dz = dx / length * speed, dz / length * speed
    end
    wish_x, wish_z = dx, dz

    -- 按下就记住，由固定步那边消费掉 —— 免得一次按键在一帧里被处理零次或两次。
    if arti.input.is_key_pressed("Space") then wants_jump = true end
    if arti.input.is_key_pressed("R") then wants_reset = true end

    -- 「站在什么上面」只在变化时报一次。每帧一条会把日志刷爆。
    local hit = ground_under(entity)
    local ground = hit and hit.uuid or nil
    if ground ~= last_ground then
        if ground then
            arti.log.info(string.format("standing on %s at y=%.2f", ground, hit.point.y))
        else
            arti.log.info("airborne")
        end
        last_ground = ground
    end
end

function on_fixed_update(entity, fixed_dt)
    if wants_reset then
        wants_reset = false
        -- 显式传送：物理位置和场景位置一起改，线 / 角速度清零。直接写 translation 是不行的 ——
        -- Dynamic 的 transform 归物理，下一步就被求解结果盖回去了。
        if spawn then
            arti.physics.teleport(entity, spawn)
        end
        return
    end

    local velocity = arti.physics.get_linear_velocity(entity)
    if not velocity then
        -- 还没有刚体：实体缺组件、被物理跳过了（带父级 / 缩放过），或者还没跑过一个固定步。
        return
    end

    -- 水平两轴直接设速度，**竖直那一轴原样留给重力** —— 一起设成 0 就变成「按住键悬空」了。
    arti.physics.set_linear_velocity(entity, { x = wish_x, y = velocity.y, z = wish_z })

    if wants_jump then
        wants_jump = false
        -- 只在踩着东西的时候跳，不然按住 Space 能一路飞上去。
        if ground_under(entity) then
            -- 冲量是**立刻**改速度，力要乘一个步长才见效 —— 跳跃要的是前者。
            arti.physics.apply_impulse(entity, { x = 0.0, y = JUMP, z = 0.0 })
        end
    end
end

function on_destroy(entity)
    arti.log.info("physics_move detached from " .. entity.name)
end
