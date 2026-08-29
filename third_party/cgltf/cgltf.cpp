// cgltf 的实现只能在一个翻译单元里展开，所以给它一个专属的 .cpp。
// 和 tiny_obj_loader 同样的处理，理由见 third_party/CMakeLists.txt 里的说明。
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
