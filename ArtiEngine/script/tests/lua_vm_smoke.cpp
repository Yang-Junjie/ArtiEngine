// Lua VM 冒烟测试：不碰引擎，只确认 sol2 + Lua 这一层是通的，以及**白名单真的是白名单**。
//
// 为什么值得单独一个测试（和 physics_smoke 同一个理由）：脚本层马上要把用户写的文本喂进这个
// VM，所以「一个手误会不会打穿进程」和「脚本能不能读硬盘」这两条必须先在不含引擎的地方钉死。
// 后面 ScriptSystem 出问题时就不用怀疑到这一层。
//
// 它同时是「lua submodule 换了个版本」的对冲：指针一动，第一个该报警的是这里。

#include <sol/sol.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace {

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "lua_vm_smoke: " << message << '\n';
    }
    return condition;
}

// ScriptSystem 只会打开这四个库。io / os / debug / package 刻意不开：脚本是数据，
// 不该能读硬盘、起进程、或者 require 进别的文件。
sol::state makeSandbox() {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    return lua;
}

int run() {
    sol::state lua = makeSandbox();

    // ---- 1. 基本求值 ----
    {
        const auto result = lua.safe_script("return 1 + 2", sol::script_pass_on_error);
        if (!require(result.valid(), "求值 1+2 失败")) {
            return 1;
        }
        if (!require(result.get<int>() == 3, "1+2 不等于 3")) {
            return 1;
        }
    }

    // ---- 2. 打开的四个库真的在 ----
    // 每个库挑一个 ScriptSystem 的绑定层或示例脚本真会用到的函数。
    if (!require(lua["math"]["floor"].valid(), "math.floor 不在")) {
        return 1;
    }
    if (!require(lua["string"]["format"].valid(), "string.format 不在")) {
        return 1;
    }
    if (!require(lua["table"]["insert"].valid(), "table.insert 不在")) {
        return 1;
    }
    if (!require(lua["pcall"].valid(), "base 库的 pcall 不在")) {
        return 1;
    }

    // ---- 3. 没打开的库必须真的不在（D3 的白名单不是装饰）----
    // 反向验证就验这一组：给 makeSandbox() 加上 sol::lib::os / io / package，对应那条必须变红。
    if (!require(!lua["os"].valid(), "os 库存在 —— 沙箱漏了，脚本能起进程 / 读时钟改文件")) {
        return 1;
    }
    if (!require(!lua["io"].valid(), "io 库存在 —— 沙箱漏了，脚本能读写任意文件")) {
        return 1;
    }
    if (!require(!lua["require"].valid(), "require 存在 —— package 库漏了，脚本能加载任意模块")) {
        return 1;
    }
    if (!require(!lua["debug"].valid(), "debug 库存在 —— 脚本能绕过一切封装")) {
        return 1;
    }

    // ---- 4. 脚本里的 error 必须被抓住，不能穿过来 ----
    // 这是整个脚本层最硬的一条：一个手误的脚本不许把编辑器带走（Stop 都按不到）。
    {
        const auto result = lua.safe_script("error('boom')", sol::script_pass_on_error);
        if (!require(!result.valid(), "error('boom') 居然被当成成功")) {
            return 1;
        }
        const sol::error error = result;
        if (!require(std::string{ error.what() }.find("boom") != std::string::npos,
                    "抓到的错误信息里没有 boom")) {
            return 1;
        }
    }

    // ---- 5. 语法错误也走同一条路 ----
    // 用户存了个写坏一半的 .lua 就属于这种，和运行期 error 不是同一段代码。
    {
        const auto result = lua.safe_script("this is not lua", sol::script_pass_on_error);
        if (!require(!result.valid(), "语法错误居然被当成成功")) {
            return 1;
        }
    }

    // ---- 6. 取一个不存在的全局函数不是错误 ----
    // 脚本约定 on_create / on_update / on_destroy 三个可选回调，只写一个是正常用法，
    // ScriptSystem 靠这个判断「要不要调」。
    {
        const sol::protected_function absent = lua["on_update"];
        if (!require(!absent.valid(), "不存在的全局函数居然是 valid 的")) {
            return 1;
        }
    }

    // ---- 7. 回调调用的往返（ScriptSystem 会走的形状）----
    {
        const auto loaded = lua.safe_script(
                "function on_update(value, dt) return value + dt end", sol::script_pass_on_error);
        if (!require(loaded.valid(), "定义 on_update 失败")) {
            return 1;
        }
        const sol::protected_function on_update = lua["on_update"];
        if (!require(on_update.valid(), "定义之后 on_update 还是取不到")) {
            return 1;
        }
        const auto called = on_update(1, 2);
        if (!require(called.valid() && called.get<int>() == 3, "on_update(1, 2) 没返回 3")) {
            return 1;
        }
    }

    // ---- 8. 每个实体一份 environment，互不污染 ----
    // 两个实体挂同一个脚本、或挂两个都定义了同名回调的脚本，不能互相盖掉 —— 所以
    // ScriptSystem 给每个实例一个独立的全局表（共享同一个 state）。
    //
    // 回调名故意用 `scoped_callback` 而不是 `on_update`：第 7 组已经把 on_update 定义进真正的
    // 全局表了，拿它来验「没漏进全局」必然红，而红的原因是测试自己的名字撞车，不是 sol2 漏了。
    // 第一版就这么写的，直接踩了一次。
    {
        sol::environment first{ lua, sol::create, lua.globals() };
        sol::environment second{ lua, sol::create, lua.globals() };
        const auto a = lua.safe_script("function scoped_callback() return 'first' end", first,
                sol::script_pass_on_error);
        const auto b = lua.safe_script("function scoped_callback() return 'second' end", second,
                sol::script_pass_on_error);
        if (!require(a.valid() && b.valid(), "在 environment 里定义函数失败")) {
            return 1;
        }
        const sol::protected_function first_fn = first["scoped_callback"];
        const sol::protected_function second_fn = second["scoped_callback"];
        if (!require(first_fn.valid() && second_fn.valid(),
                    "environment 里的 scoped_callback 取不到")) {
            return 1;
        }
        if (!require(first_fn().get<std::string>() == "first" &&
                            second_fn().get<std::string>() == "second",
                    "两个 environment 的回调串了 —— 一个实体的脚本会盖掉另一个")) {
            return 1;
        }
        // 而且不该漏进真正的全局表。
        if (!require(!lua["scoped_callback"].valid(),
                    "environment 里定义的函数漏进了全局表")) {
            return 1;
        }
        // 反过来，全局表里的东西 environment 必须看得见 —— 绑定（arti.*）就是放在全局的，
        // 每个脚本都要能用。
        if (!require(first["math"].valid(), "environment 看不见全局的 math —— 绑定会不可见")) {
            return 1;
        }
    }

    return 0;
}

} // namespace

int main() { return run(); }
