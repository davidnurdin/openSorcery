#include "native.h"
#include <map>
static std::map<uint16_t, NativeDef>& reg() { static std::map<uint16_t, NativeDef> r; return r; }
void registerNative(const NativeDef& d) { reg()[d.addr] = d; }
const NativeDef* findNative(uint16_t a) { auto it = reg().find(a); return it == reg().end() ? nullptr : &it->second; }
const std::vector<NativeDef>& allNatives() {
    static std::vector<NativeDef> v;
    v.clear();
    for (auto& kv : reg()) v.push_back(kv.second);
    return v;
}
