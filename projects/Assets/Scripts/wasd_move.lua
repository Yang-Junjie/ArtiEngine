-- WASD 推动挂着这个脚本的实体，并向下打一条射线。
--
-- 挂法：选中一个实体 → Add Component → Script → 把这个脚本的 UUID 填进去 → Play 或 Simulate。
-- Edit 模式不跑（那时候 World::tick 根本没被调），所以编辑期不会被脚本推着走。
--
-- 三个回调都是可选的，缺哪个就跳过哪个。

local SPEED = 4.0
local SPRINT = 3.0

-- 射线只在打中的实体变了的时候报一次。每帧 log 一次会把日志刷爆，
-- 而「站在什么上面」这件事只在变化时才有信息量。
local last_ground = nil

function on_create(entity)
    arti.log.info("wasd_move attached to " .. entity.name)
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
        dx, dz = dx / length, dz / length

        local t = entity.translation
        t.x = t.x + dx * speed * dt
        t.z = t.z + dz * speed * dt
        entity.translation = t
    end

    -- 从脚下往下打 5 个单位。translation 是位移向量，不是方向 —— 长度就写在里面。
    local origin = entity.translation
    local hit = arti.physics.raycast(origin, { x = 0.0, y = -5.0, z = 0.0 })
    local ground = hit and hit.uuid or nil
    if ground ~= last_ground then
        if ground then
            arti.log.info(string.format("standing over %s at y=%.2f", ground, hit.point.y))
        else
            arti.log.info("nothing under me")
        end
        last_ground = ground
    end
end

function on_destroy(entity)
    arti.log.info("wasd_move detached from " .. entity.name)
end
