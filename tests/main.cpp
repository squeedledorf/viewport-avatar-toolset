#include <cstring>

#include "check.h"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (auto& c : check::cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        int before = check::failures();
        c.fn();
        ++run;
        std::printf("%s %s\n", check::failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("%d tests, %d failed checks\n", run, check::failures());
    return check::failures() ? 1 : 0;
}
