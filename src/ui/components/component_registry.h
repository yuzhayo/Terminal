#pragma once

#include <functional>
#include <map>
#include <memory>

#include "ui/components/component.h"

namespace ui::components {

class ComponentRegistry final {
public:
    using Factory =
        std::function<std::unique_ptr<Component>(const config::ResolvedComponent&, ComponentHost&)>;

    ComponentRegistry();

    bool Supports(config::ComponentType type) const noexcept;
    std::unique_ptr<Component> CreateTree(const config::ResolvedComponent& definition,
                                          ComponentHost& host) const;

private:
    void Register(config::ComponentType type, Factory factory);

    std::map<config::ComponentType, Factory> factories_;
};

}  // namespace ui::components
