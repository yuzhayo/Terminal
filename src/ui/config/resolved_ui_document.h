#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ui::config {

enum class ThemeKind { Dark, Light, HighContrast };
enum class ThemePreference { System, Dark, Light };
enum class SystemColorSlot { Window, WindowText, GrayText, Highlight, HighlightText };

struct LiteralRgba {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    bool operator==(const LiteralRgba&) const = default;
};

using ResolvedColor = std::variant<LiteralRgba, SystemColorSlot>;

struct ValueBinding {
    std::string path;

    bool operator==(const ValueBinding&) const = default;
};

using TextValue = std::variant<std::string, ValueBinding>;
using BooleanValue = std::variant<bool, ValueBinding>;

struct EventPayloadValue {
    using Object = std::map<std::string, EventPayloadValue, std::less<>>;
    using Array = std::vector<EventPayloadValue>;
    using Storage =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, ValueBinding, Object, Array>;

    Storage value;
};

struct EventDefinition {
    std::string action;
    EventPayloadValue::Object payload;
};

enum class DimensionKind { Auto, Fill, Pixels };

struct Dimension {
    DimensionKind kind = DimensionKind::Auto;
    int pixels = 0;
};

struct Insets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct LayoutDefinition {
    Dimension width;
    Dimension height;
    int minimum_width = 0;
    int minimum_height = 0;
    int maximum_width = 8192;
    int maximum_height = 8192;
    Insets margin;
};

enum class AutomationLive { Off, Polite, Assertive };

struct AutomationDefinition {
    std::variant<std::monostate, std::string, ValueBinding> name;
    bool automatic_name = true;
    std::string help_text;
    AutomationLive live = AutomationLive::Off;
};

enum class ComponentType {
    Window,
    Screen,
    Container,
    Text,
    Button,
    Input,
    Combo,
    Checkbox,
    Toggle,
    Card,
    List,
    Scrollbar,
    Dialog,
    Tabs,
};

enum class ContainerDirection { Row, Column, Grid, Flow };
enum class ContainerAlign { Start, Center, End, Stretch };
enum class ContainerJustify { Start, Center, End, SpaceBetween };
enum class OverflowMode { Visible, Clip, Scroll };
enum class TextVariant { Body, Title, Caption, Monospace };
enum class TextAlign { Start, Center, End };
enum class ButtonVariant { Default, Primary, Subtle, Danger, Navigation, Bookmark };
enum class InputMode { SingleLine, Multiline };
enum class ScrollbarMode { Auto, Never };
enum class SelectionMode { None, Single };
enum class Orientation { Vertical, Horizontal };

struct WindowProperties {
    TextValue title;
    std::string initial_route;
    int initial_width = 760;
    int initial_height = 520;
    int minimum_width = 620;
    int minimum_height = 420;
    bool resizable = true;
};

enum class SelectMode { Multi, Single };

struct SelectRule {
    SelectMode mode = SelectMode::Multi;
    std::vector<std::string> ids;
};

struct ScreenProperties {
    std::string route_id;
    std::string tab_label;
    bool show_in_tabs = true;
    std::vector<SelectRule> select_rules;
};

struct TabsProperties {};

struct ContainerProperties {
    ContainerDirection direction = ContainerDirection::Column;
    int gap = 8;
    Insets padding;
    ContainerAlign align = ContainerAlign::Stretch;
    ContainerJustify justify = ContainerJustify::Start;
    bool wrap = false;
    OverflowMode overflow = OverflowMode::Visible;
};

struct TextProperties {
    TextValue text;
    TextVariant variant = TextVariant::Body;
    bool wrap = true;
    bool selectable = false;
    TextAlign align = TextAlign::Start;
};

struct ButtonProperties {
    TextValue label;
    ButtonVariant variant = ButtonVariant::Default;
    BooleanValue selected = false;
    bool press_selects = false;
    bool tab_stop = true;
};

struct InputProperties {
    ValueBinding value_binding;
    InputMode mode = InputMode::SingleLine;
    std::string placeholder;
    bool read_only = false;
    bool password = false;
    int maximum_length = 4096;
    TextAlign horizontal_align = TextAlign::Start;
    ScrollbarMode scrollbar = ScrollbarMode::Auto;
    bool tab_stop = true;
};

struct ComboProperties {
    ValueBinding items_binding;
    ValueBinding selected_value_binding;
    std::string placeholder;
    int maximum_visible_items = 10;
    int popup_maximum_height = 480;
    bool allow_empty = true;
    bool tab_stop = true;
};

struct CheckboxProperties {
    TextValue label;
    ValueBinding checked_binding;
    bool tri_state = false;
    bool tab_stop = true;
};

struct ToggleProperties {
    TextValue label;
    ValueBinding checked_binding;
    std::string variant = "default";
    bool tab_stop = true;
};

struct CardProperties {
    bool interactive = false;
    BooleanValue selected = false;
    bool tab_stop = false;
};

struct ResolvedComponent;

struct ListProperties {
    ValueBinding items_binding;
    std::shared_ptr<const ResolvedComponent> item_template;
    std::optional<ValueBinding> selected_id_binding;
    int row_height = 32;
    int overscan_rows = 2;
    SelectionMode selection = SelectionMode::Single;
    std::string empty_text = "Tidak ada data";
    ScrollbarMode scrollbar = ScrollbarMode::Auto;
    bool tab_stop = true;
};

struct ScrollbarProperties {
    Orientation orientation = Orientation::Vertical;
    int thickness = 12;
    int minimum_thumb_length = 24;
    int line_step = 1;
    std::optional<int> page_step;
};

struct DialogProperties {
    TextValue title;
    int width = 480;
    int maximum_height = 720;
    bool dismiss_escape = true;
    bool dismiss_outside_click = false;
    bool dismiss_explicit_action = true;
};

using ComponentProperties =
    std::variant<WindowProperties, ScreenProperties, TabsProperties, ContainerProperties, TextProperties,
                 ButtonProperties, InputProperties, ComboProperties, CheckboxProperties,
                 ToggleProperties, CardProperties, ListProperties, ScrollbarProperties,
                 DialogProperties>;

struct ResolvedComponent {
    std::string id;
    ComponentType type = ComponentType::Container;
    bool visible = true;
    bool enabled = true;
    std::string style_id;
    std::size_t style_index = 0;
    LayoutDefinition layout;
    AutomationDefinition automation;
    std::map<std::string, EventDefinition, std::less<>> events;
    ComponentProperties properties;
    std::vector<ResolvedComponent> children;
};

enum class VisualState { Normal, Hover, Pressed, Selected, Disabled, Focus };

struct ResolvedVisualState {
    ResolvedColor background;
    ResolvedColor foreground;
    ResolvedColor border;
};

struct ResolvedFont {
    std::string family;
    std::string fallback_family;
    int point_size = 9;
    int weight = 400;
};

struct ResolvedStyle {
    std::string id;
    ResolvedFont font;
    int minimum_height = 0;
    Insets content_padding;
    int radius = 0;
    int border_width = 1;
    int focus_width = 2;
    std::array<ResolvedVisualState, 6> states;
};

struct ResolvedTheme {
    ThemeKind kind = ThemeKind::Light;
    std::map<std::string, ResolvedColor, std::less<>> tokens;
    std::vector<ResolvedStyle> styles;
};

struct UiConfigMetadata {
    std::string schema;
    int version = 0;
    int minimum_reader_contract = 0;
    std::string written_by_app_version;
    int written_by_config_contract = 0;
};

struct ResolvedUiDocument {
    UiConfigMetadata metadata;
    std::uint64_t generation = 0;
    std::array<ResolvedTheme, 3> themes;
    std::map<std::string, ResolvedComponent, std::less<>> windows;
    std::map<std::string, ResolvedComponent, std::less<>> screens;

    const ResolvedTheme& theme(ThemeKind kind) const noexcept;
};

struct ResolveDiagnostic {
    std::string code;
    std::string source;
    std::string path;
    std::string message;
    bool rollback_incompatible = false;
};

namespace detail {

struct ResolveDocumentsResult {
    std::shared_ptr<const ResolvedUiDocument> document;
    std::optional<ResolveDiagnostic> override_diagnostic;
};

ResolveDocumentsResult ResolveDocuments(std::string_view embedded_json,
                                        std::optional<std::string_view> override_json,
                                        std::uint64_t generation);

}  // namespace detail

}  // namespace ui::config
