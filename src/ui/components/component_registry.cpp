#include "ui/components/component_registry.h"

#include <stdexcept>

#include "ui/components/button/button_component.h"
#include "ui/components/card/card_component.h"
#include "ui/components/checkbox/checkbox_component.h"
#include "ui/components/combo/combo_component.h"
#include "ui/components/container/container_component.h"
#include "ui/components/input/input_component.h"
#include "ui/components/screen/screen_component.h"
#include "ui/components/scrollbar/scrollbar_component.h"
#include "ui/components/text/text_component.h"
#include "ui/components/toggle/toggle_component.h"
#include "ui/components/window/window_component.h"

namespace ui::components {

ComponentRegistry::ComponentRegistry() {
    Register(config::ComponentType::Window,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<WindowComponent>(definition, host);
             });
    Register(config::ComponentType::Container,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ContainerComponent>(definition, host);
             });
    Register(config::ComponentType::Text,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<TextComponent>(definition, host);
             });
    Register(config::ComponentType::Button,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ButtonComponent>(definition, host);
             });
    Register(config::ComponentType::Input,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<InputComponent>(definition, host);
             });
    Register(config::ComponentType::Combo,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ComboComponent>(definition, host);
             });
    Register(config::ComponentType::Screen,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ScreenComponent>(definition, host);
             });
    Register(config::ComponentType::Checkbox,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<CheckboxComponent>(definition, host);
             });
    Register(config::ComponentType::Toggle,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ToggleComponent>(definition, host);
             });
    Register(config::ComponentType::Card,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<CardComponent>(definition, host);
             });
    Register(config::ComponentType::Scrollbar,
             [](const config::ResolvedComponent& definition, ComponentHost& host) {
                 return std::make_unique<ScrollbarComponent>(definition, host);
             });
}

void ComponentRegistry::Register(config::ComponentType type, Factory factory) {
    factories_.insert_or_assign(type, std::move(factory));
}

bool ComponentRegistry::Supports(config::ComponentType type) const noexcept {
    return factories_.contains(type);
}

std::unique_ptr<Component> ComponentRegistry::CreateTree(
    const config::ResolvedComponent& definition, ComponentHost& host) const {
    const auto factory = factories_.find(definition.type);
    if (factory == factories_.end()) throw std::runtime_error("Component type is not registered.");
    std::unique_ptr<Component> component = factory->second(definition, host);
    for (const config::ResolvedComponent& child : definition.children) {
        component->AddChild(CreateTree(child, host));
    }
    return component;
}

}  // namespace ui::components
