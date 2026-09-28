#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#include "vats/skeleton.h"

// The skeleton from data/character, loaded once.
inline const vats::Skeleton& skel() {
    static vats::Skeleton s = [] {
        vats::Skeleton k;
        std::string err;
        if (!k.load_dir(VATS_DATA_DIR, err)) {
            std::fprintf(stderr, "cannot load skeleton: %s\n", err.c_str());
            std::exit(2);
        }
        return k;
    }();
    return s;
}
