#include "engine_log.h"

namespace arti::engine {

const core::Logger::Channel& getLogChannel() {
    static const auto channel = core::Logger::registerChannel("ArtiEngine");
    return *channel;
}

} // namespace arti::engine
