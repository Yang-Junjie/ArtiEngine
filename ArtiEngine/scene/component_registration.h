#pragma once

namespace arti::engine {

// 每进程调一次，必须在克隆任何场景之前。SceneCloner 会跳过未注册的组件类型，
// 漏调的表现是「按一次 Play，网格和光照全没了」。
void registerSceneComponents();

} // namespace arti::engine
