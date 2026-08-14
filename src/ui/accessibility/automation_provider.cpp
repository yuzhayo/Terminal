#include "ui/accessibility/automation_provider.h"

#include <oleauto.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "ui/components/component.h"

namespace ui::accessibility {
namespace {

void SetBoolean(VARIANT* value, bool state) noexcept {
    value->vt = VT_BOOL;
    value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
}

HRESULT SetString(VARIANT* value, const std::wstring& text) noexcept {
    value->vt = VT_BSTR;
    value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return value->bstrVal || text.empty() ? S_OK : E_OUTOFMEMORY;
}

CONTROLTYPEID ControlType(components::AutomationRole role) noexcept {
    switch (role) {
        case components::AutomationRole::Button: return UIA_ButtonControlTypeId;
        case components::AutomationRole::Checkbox: return UIA_CheckBoxControlTypeId;
        case components::AutomationRole::ToggleButton: return UIA_ButtonControlTypeId;
        case components::AutomationRole::Edit: return UIA_EditControlTypeId;
        case components::AutomationRole::Combo: return UIA_ComboBoxControlTypeId;
        case components::AutomationRole::Scrollbar: return UIA_ScrollBarControlTypeId;
        case components::AutomationRole::Group: return UIA_GroupControlTypeId;
        case components::AutomationRole::None: return UIA_CustomControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

UiaRect ScreenBounds(HWND window, const RECT& client_bounds) noexcept {
    POINT origin{client_bounds.left, client_bounds.top};
    ClientToScreen(window, &origin);
    return UiaRect{static_cast<double>(origin.x), static_cast<double>(origin.y),
                   static_cast<double>(client_bounds.right - client_bounds.left),
                   static_cast<double>(client_bounds.bottom - client_bounds.top)};
}

}  // namespace

class AutomationRootProvider::ElementProvider final : public IRawElementProviderSimple,
                                                       public IRawElementProviderFragment,
                                                       public IInvokeProvider,
                                                       public IToggleProvider,
                                                       public IExpandCollapseProvider,
                                                       public IRangeValueProvider {
public:
    ElementProvider(AutomationRootProvider& root, components::Component& component,
                    std::size_t index) noexcept
        : root_(root), component_(&component), index_(index) {}

    components::Component* component() const noexcept { return component_; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == IID_IRawElementProviderFragment) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (iid == IID_IInvokeProvider && component_ &&
                   component_->automation_supports_invoke()) {
            *object = static_cast<IInvokeProvider*>(this);
        } else if (iid == IID_IToggleProvider && component_ &&
                   component_->automation_toggle_state().has_value()) {
            *object = static_cast<IToggleProvider*>(this);
        } else if (iid == IID_IExpandCollapseProvider && component_ &&
                   component_->automation_expanded().has_value()) {
            *object = static_cast<IExpandCollapseProvider*>(this);
        } else if (iid == IID_IRangeValueProvider && component_ &&
                   component_->automation_range_value().has_value()) {
            *object = static_cast<IRangeValueProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return root_.AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return root_.Release(); }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = component_ && component_->automation_native_peer()
                     ? static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                                    ProviderOptions_OverrideProvider)
                     : ProviderOptions_ServerSideProvider;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern_id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (pattern_id == UIA_InvokePatternId && component_->automation_supports_invoke()) {
            return QueryInterface(IID_IInvokeProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_TogglePatternId &&
            component_->automation_toggle_state().has_value()) {
            return QueryInterface(IID_IToggleProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_ExpandCollapsePatternId &&
            component_->automation_expanded().has_value()) {
            return QueryInterface(IID_IExpandCollapseProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_RangeValuePatternId &&
            component_->automation_range_value().has_value()) {
            return QueryInterface(IID_IRangeValueProvider, reinterpret_cast<void**>(value));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property_id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (property_id == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = ControlType(component_->automation_role());
        } else if (property_id == UIA_NamePropertyId) {
            return SetString(value, component_->automation_name());
        } else if (property_id == UIA_AutomationIdPropertyId) {
            return SetString(value, components::Utf8ToWide(component_->definition().id));
        } else if (property_id == UIA_HelpTextPropertyId) {
            return SetString(value,
                             components::Utf8ToWide(component_->definition().automation.help_text));
        } else if (property_id == UIA_ClassNamePropertyId) {
            return SetString(value, L"Terminal");
        } else if (property_id == UIA_FrameworkIdPropertyId) {
            return SetString(value, L"Terminal.Win32");
        } else if (property_id == UIA_IsEnabledPropertyId) {
            SetBoolean(value, component_->enabled());
        } else if (property_id == UIA_IsKeyboardFocusablePropertyId) {
            SetBoolean(value, component_->CanFocus());
        } else if (property_id == UIA_HasKeyboardFocusPropertyId) {
            SetBoolean(value, root_.focused_component_ &&
                                  root_.focused_component_() == component_);
        } else if (property_id == UIA_IsControlElementPropertyId ||
                   property_id == UIA_IsContentElementPropertyId) {
            SetBoolean(value, true);
        } else if (property_id == UIA_IsPasswordPropertyId) {
            SetBoolean(value, component_->automation_is_password());
        } else if (property_id == UIA_IsInvokePatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_supports_invoke());
        } else if (property_id == UIA_IsTogglePatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_toggle_state().has_value());
        } else if (property_id == UIA_IsExpandCollapsePatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_expanded().has_value());
        } else if (property_id == UIA_IsRangeValuePatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_range_value().has_value());
        } else if (property_id == UIA_NativeWindowHandlePropertyId &&
                   component_->automation_native_peer()) {
            value->vt = VT_I4;
            value->lVal = HandleToLong(component_->automation_native_peer());
        } else if (property_id == UIA_IsOffscreenPropertyId) {
            const RECT bounds = component_->bounds();
            SetBoolean(value, !component_->visible() || !IsWindowVisible(root_.window_) ||
                                  bounds.right <= bounds.left || bounds.bottom <= bounds.top);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        const HWND peer = component_->automation_native_peer();
        return peer ? UiaHostProviderFromHwnd(peer, value) : S_OK;
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent) {
            *value = static_cast<IRawElementProviderFragment*>(&root_);
            root_.AddRef();
        } else if (direction == NavigateDirection_PreviousSibling && index_ > 0) {
            *value = static_cast<IRawElementProviderFragment*>(root_.ProviderAt(index_ - 1));
            (*value)->AddRef();
        } else if (direction == NavigateDirection_NextSibling &&
                   index_ + 1 < root_.elements_.size()) {
            *value = static_cast<IRawElementProviderFragment*>(root_.ProviderAt(index_ + 1));
            (*value)->AddRef();
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!*value) return E_OUTOFMEMORY;
        LONG position = 0;
        LONG item = UiaAppendRuntimeId;
        SafeArrayPutElement(*value, &position, &item);
        position = 1;
        item = static_cast<LONG>(index_ + 1);
        SafeArrayPutElement(*value, &position, &item);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        if (!component_ || !root_.window_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = ScreenBounds(root_.window_, component_->bounds());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationFocus() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<IRawElementProviderFragmentRoot*>(&root_);
        root_.AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationInvoke() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE Toggle() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationToggle() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto state = component_->automation_toggle_state();
        if (!state) return UIA_E_INVALIDOPERATION;
        *value = *state ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Expand() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationExpand() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE Collapse() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationCollapse() ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto expanded = component_->automation_expanded();
        if (!expanded) return UIA_E_INVALIDOPERATION;
        *value = *expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationSetRangeValue(value) ? S_OK : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE get_Value(double* value) override {
        if (!value) return E_POINTER;
        const auto range = component_ ? component_->automation_range_value() : std::nullopt;
        if (!range) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = range->value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value) override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* value) override {
        return RangeMember(value, &components::AutomationRangeValue::maximum);
    }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* value) override {
        return RangeMember(value, &components::AutomationRangeValue::minimum);
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* value) override {
        return RangeMember(value, &components::AutomationRangeValue::large_change);
    }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* value) override {
        return RangeMember(value, &components::AutomationRangeValue::small_change);
    }

private:
    HRESULT RangeMember(double* value, double components::AutomationRangeValue::*member) const {
        if (!value) return E_POINTER;
        const auto range = component_ ? component_->automation_range_value() : std::nullopt;
        if (!range) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (*range).*member;
        return S_OK;
    }

    AutomationRootProvider& root_;
    components::Component* component_ = nullptr;
    std::size_t index_ = 0;
};

AutomationRootProvider::AutomationRootProvider(
    HWND window, components::Component& root,
    std::function<components::Component*()> focused_component)
    : window_(window), component_root_(&root), focused_component_(std::move(focused_component)) {
    std::vector<components::Component*> components;
    root.CollectAutomationElements(components);
    elements_.reserve(components.size());
    for (std::size_t index = 0; index < components.size(); ++index) {
        elements_.push_back(std::make_unique<ElementProvider>(*this, *components[index], index));
    }
}

AutomationRootProvider::~AutomationRootProvider() = default;

void AutomationRootProvider::Disconnect() noexcept {
    if (window_) UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
    window_ = nullptr;
    component_root_ = nullptr;
    focused_component_ = {};
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::QueryInterface(REFIID iid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
        *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (iid == IID_IRawElementProviderFragment) {
        *object = static_cast<IRawElementProviderFragment*>(this);
    } else if (iid == IID_IRawElementProviderFragmentRoot) {
        *object = static_cast<IRawElementProviderFragmentRoot*>(this);
    } else if (iid == IID_IRawElementProviderHwndOverride) {
        *object = static_cast<IRawElementProviderHwndOverride*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE AutomationRootProvider::AddRef() { return ++references_; }
ULONG STDMETHODCALLTYPE AutomationRootProvider::Release() {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::get_ProviderOptions(ProviderOptions* value) {
    if (!value) return E_POINTER;
    *value = ProviderOptions_ServerSideProvider;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetPatternProvider(PATTERNID, IUnknown** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetPropertyValue(PROPERTYID property_id,
                                                                    VARIANT* value) {
    if (!value) return E_POINTER;
    VariantInit(value);
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (property_id == UIA_ControlTypePropertyId) {
        value->vt = VT_I4;
        value->lVal = UIA_WindowControlTypeId;
    } else if (property_id == UIA_NamePropertyId) {
        wchar_t title[256]{};
        GetWindowTextW(window_, title, static_cast<int>(std::size(title)));
        return SetString(value, title);
    } else if (property_id == UIA_AutomationIdPropertyId) {
        return SetString(value, L"main-window");
    } else if (property_id == UIA_ClassNamePropertyId) {
        return SetString(value, L"Terminal");
    } else if (property_id == UIA_FrameworkIdPropertyId) {
        return SetString(value, L"Terminal.Win32");
    } else if (property_id == UIA_IsEnabledPropertyId) {
        SetBoolean(value, IsWindowEnabled(window_) != FALSE);
    } else if (property_id == UIA_IsControlElementPropertyId) {
        SetBoolean(value, true);
    } else if (property_id == UIA_IsContentElementPropertyId) {
        SetBoolean(value, true);
    } else if (property_id == UIA_IsKeyboardFocusablePropertyId) {
        SetBoolean(value, true);
    } else if (property_id == UIA_HasKeyboardFocusPropertyId) {
        SetBoolean(value, GetForegroundWindow() == window_);
    } else if (property_id == UIA_NativeWindowHandlePropertyId) {
        value->vt = VT_I4;
        value->lVal = HandleToLong(window_);
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::get_HostRawElementProvider(
    IRawElementProviderSimple** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return window_ ? UiaHostProviderFromHwnd(window_, value) : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::Navigate(NavigateDirection direction,
                                                            IRawElementProviderFragment** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction == NavigateDirection_FirstChild && !elements_.empty()) {
        *value = static_cast<IRawElementProviderFragment*>(elements_.front().get());
    } else if (direction == NavigateDirection_LastChild && !elements_.empty()) {
        *value = static_cast<IRawElementProviderFragment*>(elements_.back().get());
    }
    if (*value) (*value)->AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetRuntimeId(SAFEARRAY** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::get_BoundingRectangle(UiaRect* value) {
    if (!value) return E_POINTER;
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    RECT bounds{};
    GetWindowRect(window_, &bounds);
    *value = UiaRect{static_cast<double>(bounds.left), static_cast<double>(bounds.top),
                     static_cast<double>(bounds.right - bounds.left),
                     static_cast<double>(bounds.bottom - bounds.top)};
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetEmbeddedFragmentRoots(SAFEARRAY** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::SetFocus() {
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    ::SetFocus(window_);
    return ::GetFocus() == window_ ? S_OK : UIA_E_INVALIDOPERATION;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::get_FragmentRoot(
    IRawElementProviderFragmentRoot** value) {
    if (!value) return E_POINTER;
    *value = static_cast<IRawElementProviderFragmentRoot*>(this);
    AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::ElementProviderFromPoint(
    double x, double y, IRawElementProviderFragment** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    for (auto item = elements_.rbegin(); item != elements_.rend(); ++item) {
        const UiaRect bounds = ScreenBounds(window_, (*item)->component()->bounds());
        if (x >= bounds.left && x < bounds.left + bounds.width && y >= bounds.top &&
            y < bounds.top + bounds.height) {
            *value = static_cast<IRawElementProviderFragment*>(item->get());
            (*value)->AddRef();
            break;
        }
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetFocus(IRawElementProviderFragment** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    ElementProvider* provider = focused_component_ ? FindProvider(focused_component_()) : nullptr;
    if (provider) {
        *value = static_cast<IRawElementProviderFragment*>(provider);
        (*value)->AddRef();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AutomationRootProvider::GetOverrideProviderForHwnd(
    HWND hwnd, IRawElementProviderSimple** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
    ElementProvider* provider = FindProvider(hwnd);
    if (provider) {
        *value = static_cast<IRawElementProviderSimple*>(provider);
        (*value)->AddRef();
    }
    return S_OK;
}

AutomationRootProvider::ElementProvider* AutomationRootProvider::FindProvider(
    const components::Component* component) const noexcept {
    const auto found = std::find_if(elements_.begin(), elements_.end(),
                                    [component](const auto& provider) {
                                        return provider->component() == component;
                                    });
    return found == elements_.end() ? nullptr : found->get();
}

AutomationRootProvider::ElementProvider* AutomationRootProvider::FindProvider(HWND hwnd) const noexcept {
    if (!hwnd) return nullptr;
    const auto found = std::find_if(elements_.begin(), elements_.end(), [hwnd](const auto& provider) {
        return provider->component()->automation_native_peer() == hwnd;
    });
    return found == elements_.end() ? nullptr : found->get();
}

AutomationRootProvider::ElementProvider* AutomationRootProvider::ProviderAt(
    std::size_t index) const noexcept {
    return index < elements_.size() ? elements_[index].get() : nullptr;
}

}  // namespace ui::accessibility
