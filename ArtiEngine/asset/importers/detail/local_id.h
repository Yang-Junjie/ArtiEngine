#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

namespace arti::engine::asset::detail {

// 子资产 local_id 的分配。
//
// identity 是 (source_path, local_id)，handle 靠它在重导入时复用。所以 local_id
// 的稳定性直接决定场景引用会不会错位：用下标的话，在 glTF 里插入一个 mesh 就会让
// ".mesh.0" 指向另一块几何，而 handle 照旧复用 —— 不报错，只是渲染不对。
//
// 因此优先用源文件里的名字。前提是名字在源文件内不重复；重名的那几个之间仍然
// 位置相关，这一点 importer 单方面解决不了（Unity 对 FBX 子资产也是这个立场）。
class LocalIdAllocator {
public:
    // kind 区分类别（"mesh" / "material" / "texture"），避免同源不同类撞名。
    // name 为空时回退成 kind + index，此时身份是位置相关的。
    std::string allocate(std::string_view kind, std::string_view name, size_t index) {
        std::string id{ kind };
        id += '.';
        if (name.empty()) {
            id += std::to_string(index);
        } else {
            id += name;
        }

        const size_t seen = ++m_counts[id];
        if (seen == 1) {
            return id;
        }
        // 同名冲突：追加序号，保证同一份 sidecar 内 local_id 唯一。
        return id + "#" + std::to_string(seen);
    }

private:
    std::unordered_map<std::string, size_t> m_counts;
};

}
