#include "ir.h"

IRFunction* IRModule::find_function(const std::string& name) {
    for (auto& fn : functions)
        if (fn.name == name) return &fn;
    return nullptr;
}

IRStructDef* IRModule::find_struct(const std::string& name) {
    for (auto& s : structs)
        if (s.name == name) return &s;
    return nullptr;
}

IRStructDef* IRModule::add_struct(std::string name) {
    structs.push_back({std::move(name)});
    return &structs.back();
}

IRGlobal* IRModule::find_global(const std::string& name) {
    for (auto& g : globals)
        if (g.name == name) return &g;
    return nullptr;
}
