#pragma once
#include "artichoco/core/log.h"

namespace arti::engine {

// ArtiEngine 层自己的日志通道，和 ArtiChoco 各模块（ArtiScene / ArtiAsset / ...）一个套路。
//
// 刻意不走 Application::get().getLogChannel()：这一层被 asset_tools 那样的命令行工具消费，
// 那里根本没有 Application，取单例会直接断言。
const core::Logger::Channel& getLogChannel();

} // namespace arti::engine
