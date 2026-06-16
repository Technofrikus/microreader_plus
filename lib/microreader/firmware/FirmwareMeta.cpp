#include "FirmwareMeta.h"

namespace microreader {

__attribute__((used))
const FirmwareMeta g_firmware_meta = {
    {'M', 'R', 'D', 'R', 'F', 'W', '_', '_'},
    1,
    kModelBitX4 | kModelBitX3,
};

const FirmwareMeta* firmware_meta() {
  return &g_firmware_meta;
}

}  // namespace microreader
