#pragma once
#include <cstdint>
#include <filesystem>

namespace arti::editor {

class EditorContext;

// 编辑器正在编辑的场景「文档」：新建、存、读，以及和 ProjectInfo::last_open_scene 的同步。
// 场景和读写它的 serializer 都在 engine::World 里 —— 这里只管「哪个文件」这件编辑器专属的事：
// 文件对话框、当前文件名、脏标记、以及记住上次打开的场景。
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

    // 有未保存的改动。**由撤销历史推导**：当前状态编号 ≠ 存盘时记下的那个编号。
    //
    // 所以它是精确的 —— 以前这个标记只在「拖资产进 Viewport」那一处置位，Inspector 改数、拖
    // gizmo、建 / 删 / 复制实体全都不算脏。而且**撤销回存盘时的那个状态会自动变回干净**，
    // 这是编号方案比「提交就置脏」强的地方。
    bool isDirty() const noexcept;

    // 强制置脏。场景**之外**的改动（将来的项目设置之类）可以用它；场景内的改动不需要报到 ——
    // 历史栈已经在算了。0 是不可能出现的状态编号（编号从 1 开始）。
    void markDirty() noexcept { m_saved_state_id = 0; }

private:
    // 换场景前的公共动作：退 Play、清实体、清选中、清文件名。
    void reset();

    // 拿一份新的撤销基线，并把「存盘时是哪个状态」对齐到它（于是场景变干净）。
    //
    // **必须在场景已经摆好之后调。** 只在 reset() 里调的话，基线会是那个空场景，
    // populateDefault() 摆进去的默认场景就成了「一次未提交的改动」，第一次 Ctrl+Z 会把它抹掉。
    void resetHistoryBaseline();

    void populateDefault();

    // 读 path 到场景里。失败时清空场景 —— 不留半个读进来的场景假装成功。
    bool load(const std::filesystem::path& path);

    // 按值传 path：save() 传的就是 m_file，而这里要写 m_file。
    bool write(std::filesystem::path path);

    // 把 path 记进 ProjectInfo::last_open_scene，下次打开项目能回到这个场景。
    void rememberInProject(const std::filesystem::path& path) const;

    EditorContext* m_context{ nullptr };

    std::filesystem::path m_file;
    // 存盘那一刻的撤销状态编号。见 isDirty()。
    std::uint64_t m_saved_state_id{ 0 };
};

} // namespace arti::editor
