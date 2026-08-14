#include "features/ui_config_draft.h"

#include <cwchar>
#include <utility>

#include "platform/files.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "platform/strings.h"
#include "storage/json.h"

namespace features {
namespace {

const wchar_t* kPaletteKeys[] = {
    L"window", L"surface", L"surfaceAlt", L"border", L"borderStrong",
    L"text", L"textDim", L"textMuted", L"accent", L"accentHover",
    L"accentText", L"danger", L"success", L"input", L"selection"
};
const wchar_t* kRadiusKeys[] = {
    L"button", L"primaryButton", L"subtleButton", L"dangerButton",
    L"navigationButton", L"input", L"combo", L"checkbox",
    L"bookmarkCell", L"profileCard"
};
const wchar_t* kComponentKeys[] = {
    L"button", L"primaryButton", L"subtleButton", L"dangerButton",
    L"navigationButton", L"input", L"combo", L"checkbox",
    L"bookmarkCell", L"profileCard"
};
const wchar_t* kComponentSlots[] = {
    L"background", L"foreground", L"border", L"hoverBackground",
    L"hoverBorder", L"pressedBackground", L"selectedBackground", L"selectedBorder",
    L"checkedBackground", L"checkedForeground", L"disabledBackground", L"disabledForeground",
    L"focusBorder"
};

std::vector<std::wstring> Path(std::initializer_list<const wchar_t*> parts) {
    std::vector<std::wstring> out;
    for (const wchar_t* p : parts) out.emplace_back(p);
    return out;
}

std::wstring DisplayName(std::wstring_view key) {
    std::wstring out;
    for (wchar_t c : key) {
        if (c >= L'A' && c <= L'Z' && !out.empty()) out += L' ';
        out += c;
    }
    if (!out.empty()) out[0] = static_cast<wchar_t>(towupper(out[0]));
    return out;
}

bool ParseHexColor(std::wstring_view value) {
    if (value.size() != 7 || value[0] != L'#') return false;
    for (size_t i = 1; i < value.size(); ++i) {
        const wchar_t c = value[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
              (c >= L'A' && c <= L'F'))) return false;
    }
    return true;
}

json::Value* ValueAt(UiDraft* draft, const std::vector<std::wstring>& path, bool create) {
    json::Value* current = &draft->root;
    for (size_t i = 0; i < path.size(); ++i) {
        json::Value* next = current->Find(path[i]);
        if (!next && create) {
            current->Set(path[i], i + 1 == path.size()
                ? json::Value::String(L"") : json::Value::Object());
            next = current->Find(path[i]);
        }
        if (!next) return nullptr;
        current = next;
    }
    return current;
}

const json::Value* ValueAt(const UiDraft& draft, const std::vector<std::wstring>& path) {
    const json::Value* current = &draft.root;
    for (const std::wstring& part : path) {
        current = current->Find(part);
        if (!current) return nullptr;
    }
    return current;
}

// In-memory preview: swap root into the process-wide config.
// Here we store the "active" draft as a module-level variable so Leave can reload.
UiDraft g_active_draft;

bool ApplyDraftToMemory(const UiDraft& draft, std::wstring* error) {
    // Core has no ui::config namespace. We keep the draft in g_active_draft and
    // expose it via GetActiveDraft() for the frontend to consume.
    g_active_draft = draft;
    (void)error;
    return true;
}

void ReloadFileToMemory() {
    UiDraft loaded;
    std::wstring error;
    LoadDraft(&loaded, &error);
    g_active_draft = std::move(loaded);
}

}  // namespace

// ---- field schema ----

std::vector<UiField> BuildFieldSchema(bool editing_light) {
    std::vector<UiField> fields;

    auto add = [&](const wchar_t* section, const wchar_t* label,
                   std::vector<std::wstring> path, UiFieldKind kind,
                   int mn = 0, int mx = 0) {
        UiField f;
        f.section   = section;
        f.label     = label;
        f.path      = std::move(path);
        f.kind      = kind;
        f.min_value = mn;
        f.max_value = mx;
        fields.push_back(std::move(f));
    };

    add(L"General", L"UI font face",        Path({L"font", L"uiFace"}),           UiFieldKind::Text);
    add(L"General", L"Mono font face",       Path({L"font", L"monoFace"}),         UiFieldKind::Text);
    add(L"General", L"Mono fallback face",   Path({L"font", L"monoFallbackFace"}), UiFieldKind::Text);
    add(L"General", L"UI point size",        Path({L"font", L"uiPointSize"}),      UiFieldKind::Integer, 6, 36);
    add(L"General", L"Title point size",     Path({L"font", L"titlePointSize"}),   UiFieldKind::Integer, 6, 48);
    add(L"General", L"Mono point size",      Path({L"font", L"monoPointSize"}),    UiFieldKind::Integer, 6, 36);
    add(L"General", L"Initial width",        Path({L"window", L"initialWidth"}),   UiFieldKind::Integer, 480, 3840);
    add(L"General", L"Initial height",       Path({L"window", L"initialHeight"}),  UiFieldKind::Integer, 320, 2160);
    add(L"General", L"Minimum width",        Path({L"window", L"minWidth"}),       UiFieldKind::Integer, 420, 3840);
    add(L"General", L"Minimum height",       Path({L"window", L"minHeight"}),      UiFieldKind::Integer, 280, 2160);
    add(L"Layout",  L"Content alignment",    Path({L"layout", L"contentAlignment"}),      UiFieldKind::Text);
    add(L"Layout",  L"Form row",             Path({L"layout", L"formRow"}),               UiFieldKind::Text);
    add(L"Layout",  L"Equal target buttons", Path({L"layout", L"equalTargetButtons"}),    UiFieldKind::Text);
    add(L"Layout",  L"Footer alignment",     Path({L"layout", L"footerAlignment"}),       UiFieldKind::Text);
    add(L"Layout",  L"Bookmark flow",        Path({L"layout", L"bookmarkFlow"}),          UiFieldKind::Text);
    add(L"Layout",  L"Profile grid",         Path({L"layout", L"profileGrid"}),           UiFieldKind::Text);

    for (const wchar_t* key : kRadiusKeys) {
        add(L"Radius", DisplayName(key).c_str(), Path({L"radius", key}), UiFieldKind::Integer, 0, 100);
    }
    const wchar_t* theme = editing_light ? L"light" : L"dark";
    for (const wchar_t* key : kPaletteKeys) {
        add(L"Colors", DisplayName(key).c_str(), Path({L"themes", theme, key}), UiFieldKind::Color);
    }
    for (const wchar_t* comp : kComponentKeys) {
        for (const wchar_t* slot : kComponentSlots) {
            std::wstring lbl = DisplayName(comp) + L" · " + DisplayName(slot);
            add(L"Components", lbl.c_str(),
                Path({L"components", comp, theme, slot}), UiFieldKind::Color);
        }
    }
    return fields;
}

// ---- file operations ----

bool LoadDraft(UiDraft* out, std::wstring* error) {
    const std::wstring path = paths::UiConfigFile();
    if (!paths::FileExists(path)) {
        return DefaultsDraft(out, error);
    }
    std::wstring text;
    if (!files::ReadText(path, &text, error)) return false;
    json::Value root;
    if (!json::Parse(text, &root, error)) {
        if (error) *error = L"ui.json is not valid JSON:\n" + *error;
        return false;
    }
    if (!root.is_object()) {
        if (error) *error = L"ui.json must contain a JSON object.";
        return false;
    }
    out->root   = std::move(root);
    out->loaded = true;
    return true;
}

bool DefaultsDraft(UiDraft* out, std::wstring* error) {
    // Core ships with a minimal built-in default. The frontend may replace this
    // with its own richer defaults by calling DefaultsDraft with its own JSON text.
    (void)error;
    out->root   = json::Value::Object();
    out->loaded = true;
    return true;
}

std::wstring SerializeDraft(const UiDraft& draft) {
    return json::Serialize(draft.root);
}

bool EnsureUserConfigPath(std::wstring* path, std::wstring* error) {
    const std::wstring dir = paths::AppDataDir();
    if (!paths::EnsureDirectory(dir)) {
        if (error) *error = L"Cannot create the app-data directory.";
        return false;
    }
    *path = paths::UiConfigFile();
    return true;
}

std::wstring UserConfigPath() { return paths::UiConfigFile(); }
bool UserConfigExists()       { return paths::FileExists(paths::UiConfigFile()); }

// ---- validation ----

bool ValidateField(UiField* field, UiDraft* draft, std::wstring* first_error) {
    field->error.clear();
    const std::wstring text = str::Trim(field->current_text);
    json::Value value;

    if (field->kind == UiFieldKind::Integer) {
        wchar_t* end = nullptr;
        const long parsed = wcstol(text.c_str(), &end, 10);
        if (text.empty() || !end || *end != L'\0' ||
            parsed < field->min_value || parsed > field->max_value) {
            field->error = L"Use " + std::to_wstring(field->min_value) +
                           L".." + std::to_wstring(field->max_value);
        } else {
            value = json::Value::Number(static_cast<double>(parsed));
        }
    } else if (field->kind == UiFieldKind::Color) {
        const bool component = !field->path.empty() && field->path[0] == L"components";
        if (!component && !ParseHexColor(text)) {
            field->error = L"Use #RRGGBB";
        } else if (component && text.empty()) {
            field->error = L"Use #RRGGBB, a palette token, or transparent";
        } else {
            value = json::Value::String(text);
        }
    } else {
        // Text
        if (text.empty()) {
            field->error = L"Required";
        } else if (field->path.size() == 2 &&
                   field->path[0] == L"layout" &&
                   field->path[1] == L"equalTargetButtons") {
            if (text != L"true" && text != L"false") {
                field->error = L"Use true or false";
            } else {
                value = json::Value::Bool(text == L"true");
            }
        } else {
            value = json::Value::String(text);
        }
    }

    if (!field->error.empty()) {
        if (first_error && first_error->empty())
            *first_error = field->label + L": " + field->error;
        return false;
    }
    json::Value* target = ValueAt(draft, field->path, true);
    if (target) *target = std::move(value);
    return true;
}

bool PullFields(std::vector<UiField>* fields, UiDraft* draft, std::wstring* first_error) {
    bool valid = true;
    for (UiField& field : *fields)
        valid = ValidateField(&field, draft, first_error) && valid;
    return valid;
}

void PopulateFields(std::vector<UiField>* fields, const UiDraft& draft) {
    for (UiField& field : *fields) {
        const json::Value* v = ValueAt(draft, field.path);
        if (!v) { field.current_text.clear(); continue; }
        if (v->is_string())      field.current_text = v->AsString();
        else if (v->is_number()) field.current_text = std::to_wstring(static_cast<int>(v->AsNumber()));
        else if (v->is_bool())   field.current_text = v->AsBool() ? L"true" : L"false";
        else                     field.current_text.clear();
        field.error.clear();
    }
}

// ---- enumerated constraints ----

void SetDensity(UiDraft* draft, const std::wstring& value) {
    const std::wstring v = (value == L"compact" || value == L"comfortable") ? value : L"default";
    draft->root.Set(L"density", json::Value::String(v));
}

void SetCornerPreference(UiDraft* draft, const std::wstring& value) {
    const std::wstring v = (value == L"square" || value == L"rounded" || value == L"small-rounded")
        ? value : L"default";
    json::Value* window = draft->root.Find(L"window");
    if (!window) {
        draft->root.Set(L"window", json::Value::Object());
        window = draft->root.Find(L"window");
    }
    if (window) window->Set(L"cornerPreference", json::Value::String(v));
}

std::wstring GetDensity(const UiDraft& draft) {
    return draft.root.StringField(L"density", L"default");
}

std::wstring GetCornerPreference(const UiDraft& draft) {
    const json::Value* window = draft.root.ObjectField(L"window");
    return window ? window->StringField(L"cornerPreference", L"rounded") : L"rounded";
}

// ---- state machine ----

bool Preview(UiEditorState* state, std::vector<UiField>* fields, std::wstring* error) {
    if (!PullFields(fields, &state->draft, error)) return false;
    if (!ApplyDraftToMemory(state->draft, error)) return false;
    state->previewed = true;
    return true;
}

core::Status ApplyAndSave(UiEditorState* state, std::vector<UiField>* fields) {
    std::wstring error;
    if (!PullFields(fields, &state->draft, &error)) return core::Error(error);
    if (!ApplyDraftToMemory(state->draft, &error)) return core::Error(error);

    std::wstring path;
    if (!EnsureUserConfigPath(&path, &error)) {
        ReloadFileToMemory();
        return core::Error(error);
    }
    std::wstring io_error;
    if (!files::MakeBackup(path, &io_error) ||
        !files::WriteTextInPlace(path, SerializeDraft(state->draft), &io_error)) {
        // Rollback: re-apply the last saved config.
        ReloadFileToMemory();
        return core::Error(io_error);
    }
    state->opened   = state->draft;
    state->previewed = false;
    return core::Success(L"Saved ui.json in place. Recovery copy: ui.json.otn.bak");
}

core::Status ResetDraft(UiEditorState* state) {
    state->draft = state->opened;
    return core::Success(L"Draft reset.");
}

core::Status UseDefaults(UiEditorState* state) {
    std::wstring error;
    if (!DefaultsDraft(&state->draft, &error)) return core::Error(error);
    return core::Info(L"Defaults loaded into the draft. Nothing written yet.");
}

void Leave(UiEditorState* state, bool restore_file) {
    if (restore_file && state->previewed)
        ReloadFileToMemory();
    state->previewed = false;
}

}  // namespace features
