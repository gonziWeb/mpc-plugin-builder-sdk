// SPDX-License-Identifier: GPL-3.0-only
// Armada: a pack's engines, their parameters (name, range, the text of each end) and their sounds.
//   c++ -std=c++17 -Isrc tools/pack_info.cpp src/pk_<name>.cpp -DPACK=armada_pack_<name> -o pack_info
#include "armada_pack.h"
#include <cstdio>
#include <cstring>
extern const armada::Pack PACK;
int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    armada::Part *p = PACK.create();
    for (int e = 0; e < PACK.numEngines(); e++) {
        const armada::EngineInfo *ei = PACK.engine(e);
        if (only && std::strcmp(only, ei->name)) continue;
        if (!std::strcmp(ei->name, "-")) continue;
        std::printf("%s (%d sounds):", ei->name, ei->numSounds);
        p->loadSound(e, 0, nullptr);
        for (int k = 0; k < ei->numParams; k++) {
            char a[40] = "", b[40] = "";
            if (!ei->p[k].label[0]) continue;
            p->format(e, k, ei->p[k].min, a, sizeof a);
            p->format(e, k, ei->p[k].max, b, sizeof b);
            std::printf(" %s[%s..%s]", ei->p[k].label, a, b);
        }
        std::printf("\n    sounds:");
        for (int s = 0; s < ei->numSounds && s < (argc > 2 ? 300 : 12); s++) std::printf(" %d:%s", s, PACK.soundName(e, s));
        std::printf("\n");
    }
    delete p;
    return 0;
}
