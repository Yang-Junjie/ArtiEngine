#pragma once
#include <filesystem>
#include <memory>

namespace arti::scene {
class SceneSerializationRegistry;
class SceneSerializer;
} // namespace arti::scene

namespace arti::editor {

class EditorContext;

// 编辑器正在编辑的场景「文档」：新建、存、读，以及和 ProjectInfo::last_open_scene 的同步。
// 场景本身在 EditorContext 里 —— 这里只管它和磁盘之间的那条路。
//
// 所有换场景的入口都从 reset() 走，那里会退出 Play 模式并清掉选中的实体：
// 快照和选中 ID 指的是马上要被清掉的实体，留着就是悬空引用。
class SceneDocument {
public:
    explicit SceneDocument(EditorContext& context);
    ~SceneDocument();

    SceneDocument(const SceneDocument&) = delete;
    SceneDocument& operator=(const SceneDocument&) = delete;

    // 清空场景并摆一个默认场景（相机 + 平行光 + 环境 + 三个立方体）。
    void createNew();

    // 弹对话框选文件后读入。取消或读失败都不会留下半个场景。
    void open();

    // 存回当前文件。还没有当前文件（新建的场景）时转成 saveAs()。返回是否真的存了。
    bool save();

    // 弹对话框选路径后存。返回是否真的存了。
    bool saveAs();

    // 按 ProjectInfo::last_open_scene 恢复场景。返回是否真的读进来了 ——
    // 返回 false 的时候调用方要退回 createNew()。
    bool loadLastOpen();

    // 当前场景文件。空 = 还没存过（save() 会转成 saveAs()）。
    const std::filesystem::path& file() const noexcept { return m_file; }

    // 有未保存的改动。现在只在明确的编辑动作后置位，不做逐字段脏检查 ——
    // 那需要在 Inspector 和 gizmo 的每个写入点埋钩子，先做个够用的版本。
    bool isDirty() const noexcept { return m_dirty; }
    void markDirty() noexcept { m_dirty = true; }

private:
    // 换场景前的公共动作：退 Play、清实体、清选中、清文件名和脏标记。
    void reset();

    void populateDefault();

    // 读 path 到场景里。失败时清空场景 —— 不留半个读进来的场景假装成功。
    bool load(const std::filesystem::path& path);

    // 按值传 path：save() 传的就是 m_file，而这里要写 m_file。
    bool write(std::filesystem::path path);

    // 把 path 记进 ProjectInfo::last_open_scene，下次打开项目能回到这个场景。
    void rememberInProject(const std::filesystem::path& path) const;

    EditorContext* m_context{ nullptr };

    // 序列化表和 serializer。表不是进程级的（是个对象），所以编辑器自己持有一份。
    std::unique_ptr<scene::SceneSerializationRegistry> m_serialization;
    std::unique_ptr<scene::SceneSerializer> m_serializer;

    std::filesystem::path m_file;
    bool m_dirty{ false };
};

} // namespace arti::editor
