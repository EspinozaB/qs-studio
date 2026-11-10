#include "qs_factory.h"

namespace qs {

#include "qs_factory_names.inc"

const char* factoryProgramName(int bank, int program) {
    if (bank < 1 || bank > 4 || program < 0 || program >= 128) return "";
    return kFactoryProgramNames[bank - 1][program];
}

const char* factoryMixName(int bank, int mix) {
    if (bank < 1 || bank > 4 || mix < 0 || mix >= 100) return "";
    return kFactoryMixNames[bank - 1][mix];
}

}  // namespace qs
