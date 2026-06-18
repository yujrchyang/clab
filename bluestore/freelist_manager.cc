#include "bluestore/freelist_manager.h"
#include "bluestore/bitmap_freelist_manager.h"

namespace TOPNSPC {

FreelistManager *FreelistManager::create(const std::string &type,
                                         const std::string &meta_prefix,
                                         const std::string &bitmap_prefix) {
    if (type == "bitmap") {
        return new BitmapFreelistManager(meta_prefix, bitmap_prefix);
    }
    return nullptr;
}

}  // namespace TOPNSPC
