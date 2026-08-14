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
        case components::AutomationRole::List: return UIA_ListControlTypeId;
        case components::AutomationRole::Scrollbar: return UIA_ScrollBarControlTypeId;
        case components::AutomationRole::Group: return UIA_GroupControlTypeId;
        case components::AutomationRole::Dialog: return UIA_WindowControlTypeId;
        case components::AutomationRole::None: return UIA_CustomControlTypeId;
    }
    return UIA_CustomControlTypeId;
}

components::AutomationScrollAmount ConvertScrollAmount(ScrollAmount amount) noexcept {
    switch (amount) {
        case ScrollAmount_LargeDecrement:
            return components::AutomationScrollAmount::LargeDecrement;
        case ScrollAmount_SmallDecrement:
            return components::AutomationScrollAmount::SmallDecrement;
        case ScrollAmount_LargeIncrement:
            return components::AutomationScrollAmount::LargeIncrement;
        case ScrollAmount_SmallIncrement:
            return components::AutomationScrollAmount::SmallIncrement;
        case ScrollAmount_NoAmount:
        default:
            return components::AutomationScrollAmount::NoAmount;
    }
}

UiaRect ScreenBounds(HWND window, const RECT& client_bounds) noexcept {
    POINT origin{client_bounds.left, client_bounds.top};
    ClientToScreen(window, &origin);
    return UiaRect{static_cast<double>(origin.x), static_cast<double>(origin.y),
                   static_cast<double>(client_bounds.right - client_bounds.left),
                   static_cast<double>(client_bounds.bottom - client_bounds.top)};
}

bool SameComIdentity(IUnknown* left, IUnknown* right) noexcept {
    if (!left || !right) return left == right;
    IUnknown* left_identity = nullptr;
    IUnknown* right_identity = nullptr;
    const HRESULT left_result = left->QueryInterface(IID_IUnknown,
                                                      reinterpret_cast<void**>(&left_identity));
    const HRESULT right_result = right->QueryInterface(IID_IUnknown,
                                                        reinterpret_cast<void**>(&right_identity));
    const bool same = SUCCEEDED(left_result) && SUCCEEDED(right_result) &&
                      left_identity == right_identity;
    if (left_identity) left_identity->Release();
    if (right_identity) right_identity->Release();
    return same;
}

}  // namespace

class AutomationRootProvider::ElementProvider final : public IRawElementProviderSimple,
                                                       public IRawElementProviderFragment,
                                                       public IInvokeProvider,
                                                       public IToggleProvider,
                                                       public IExpandCollapseProvider,
                                                       public IRangeValueProvider,
                                                       public IItemContainerProvider,
                                                       public ISelectionProvider,
                                                       public IScrollProvider,
                                                       public IWindowProvider,
                                                       public ITransformProvider {
public:
    ElementProvider(AutomationRootProvider& root, components::Component& component,
                    std::size_t index) noexcept
        : root_(root), component_(&component), index_(index) {}

    components::Component* component() const noexcept { return component_; }
    std::size_t index() const noexcept { return index_; }
    void Disconnect() noexcept { component_ = nullptr; }

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
        } else if (iid == IID_IItemContainerProvider && component_ &&
                   component_->automation_supports_item_container()) {
            *object = static_cast<IItemContainerProvider*>(this);
        } else if (iid == IID_ISelectionProvider && component_ &&
                   component_->automation_supports_selection()) {
            *object = static_cast<ISelectionProvider*>(this);
        } else if (iid == IID_IScrollProvider && component_ &&
                   component_->automation_scroll_state().has_value()) {
            *object = static_cast<IScrollProvider*>(this);
        } else if (iid == IID_IWindowProvider && component_ && component_->automation_is_dialog()) {
            *object = static_cast<IWindowProvider*>(this);
        } else if (iid == IID_ITransformProvider && component_ && component_->automation_is_dialog()) {
            *object = static_cast<ITransformProvider*>(this);
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
        if (pattern_id == UIA_ItemContainerPatternId &&
            component_->automation_supports_item_container()) {
            return QueryInterface(IID_IItemContainerProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_SelectionPatternId && component_->automation_supports_selection()) {
            return QueryInterface(IID_ISelectionProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_ScrollPatternId && component_->automation_scroll_state()) {
            return QueryInterface(IID_IScrollProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_WindowPatternId && component_->automation_is_dialog()) {
            return QueryInterface(IID_IWindowProvider, reinterpret_cast<void**>(value));
        }
        if (pattern_id == UIA_TransformPatternId && component_->automation_is_dialog()) {
            return QueryInterface(IID_ITransformProvider, reinterpret_cast<void**>(value));
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
            SetBoolean(value, component_->enabled() && root_.IsReachable(component_));
        } else if (property_id == UIA_IsKeyboardFocusablePropertyId) {
            SetBoolean(value, component_->CanFocus() && root_.IsReachable(component_));
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
        } else if (property_id == UIA_IsItemContainerPatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_supports_item_container());
        } else if (property_id == UIA_IsSelectionPatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_supports_selection());
        } else if (property_id == UIA_IsScrollPatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_scroll_state().has_value());
        } else if (property_id == UIA_IsWindowPatternAvailablePropertyId ||
                   property_id == UIA_IsTransformPatternAvailablePropertyId) {
            SetBoolean(value, component_->automation_is_dialog());
        } else if (property_id == UIA_IsDialogPropertyId) {
            SetBoolean(value, component_->automation_is_dialog());
        } else if (property_id == UIA_NativeWindowHandlePropertyId &&
                   component_->automation_native_peer()) {
            value->vt = VT_I4;
            value->lVal = HandleToLong(component_->automation_native_peer());
        } else if (property_id == UIA_IsOffscreenPropertyId) {
            const RECT bounds = component_->automation_bounds();
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
            if (ElementProvider* parent = root_.AccessibleParent(this)) {
                *value = static_cast<IRawElementProviderFragment*>(parent);
                (*value)->AddRef();
            } else {
                *value = static_cast<IRawElementProviderFragment*>(&root_);
                root_.AddRef();
            }
        } else if ((direction == NavigateDirection_FirstChild ||
                    direction == NavigateDirection_LastChild) &&
                   component_->automation_supports_item_container()) {
            if (direction == NavigateDirection_FirstChild) {
                for (std::size_t item = 0; item < component_->automation_item_count(); ++item) {
                    if (component_->automation_item_realized(item)) {
                        *value = root_.ItemProviderFragment(*this, item);
                        break;
                    }
                }
            } else {
                for (std::size_t item = component_->automation_item_count(); item > 0; --item) {
                    if (component_->automation_item_realized(item - 1)) {
                        *value = root_.ItemProviderFragment(*this, item - 1);
                        break;
                    }
                }
            }
            if (*value) (*value)->AddRef();
        } else if ((direction == NavigateDirection_FirstChild ||
                    direction == NavigateDirection_LastChild) &&
                   component_->automation_has_popup_fragment() &&
                   component_->automation_popup_visible()) {
            *value = root_.PopupProviderFragment(component_);
            if (*value) (*value)->AddRef();
        } else if (direction == NavigateDirection_FirstChild ||
                   direction == NavigateDirection_LastChild) {
            const std::vector<ElementProvider*> children = root_.AccessibleChildren(this);
            if (!children.empty()) {
                *value = static_cast<IRawElementProviderFragment*>(
                    direction == NavigateDirection_FirstChild ? children.front()
                                                               : children.back());
                (*value)->AddRef();
            }
        } else if (direction == NavigateDirection_PreviousSibling ||
                   direction == NavigateDirection_NextSibling) {
            const std::vector<ElementProvider*> siblings =
                root_.AccessibleChildren(root_.AccessibleParent(this));
            const auto found = std::find(siblings.begin(), siblings.end(), this);
            if (found != siblings.end()) {
                if (direction == NavigateDirection_PreviousSibling && found != siblings.begin()) {
                    *value = static_cast<IRawElementProviderFragment*>(*(found - 1));
                } else if (direction == NavigateDirection_NextSibling && found + 1 != siblings.end()) {
                    *value = static_cast<IRawElementProviderFragment*>(*(found + 1));
                }
                if (*value) (*value)->AddRef();
            }
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
        *value = ScreenBounds(root_.window_, component_->automation_bounds());
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        if (!component_ || !root_.IsReachable(component_)) return UIA_E_ELEMENTNOTAVAILABLE;
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

    HRESULT STDMETHODCALLTYPE FindItemByProperty(IRawElementProviderSimple* start_after,
                                                  PROPERTYID property_id, VARIANT value,
                                                  IRawElementProviderSimple** found) override {
        if (!found) return E_POINTER;
        *found = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        std::size_t start = 0;
        if (start_after) {
            bool matched = false;
            for (std::size_t index = 0; index < component_->automation_item_count(); ++index) {
                IRawElementProviderSimple* candidate = root_.ItemProviderSimple(*this, index);
                if (SameComIdentity(candidate, start_after)) {
                    start = index + 1;
                    matched = true;
                    break;
                }
            }
            if (!matched) return E_INVALIDARG;
        }
        for (std::size_t index = start; index < component_->automation_item_count(); ++index) {
            bool matches = property_id == 0;
            if (property_id == UIA_NamePropertyId && value.vt == VT_BSTR) {
                matches = component_->automation_item_name(index) ==
                          std::wstring(value.bstrVal ? value.bstrVal : L"");
            } else if (property_id == UIA_AutomationIdPropertyId && value.vt == VT_BSTR) {
                const std::wstring id = components::Utf8ToWide(component_->definition().id) +
                                        L"-item-" + std::to_wstring(index);
                matches = id == std::wstring(value.bstrVal ? value.bstrVal : L"");
            } else if (property_id != 0 && property_id != UIA_NamePropertyId &&
                       property_id != UIA_AutomationIdPropertyId) {
                return E_INVALIDARG;
            }
            if (matches) {
                *found = root_.ItemProviderSimple(*this, index);
                (*found)->AddRef();
                break;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        std::optional<std::size_t> selected;
        for (std::size_t index = 0; index < component_->automation_item_count(); ++index) {
            if (component_->automation_item_selected(index)) {
                selected = index;
                break;
            }
        }
        *value = SafeArrayCreateVector(VT_UNKNOWN, 0, selected ? 1 : 0);
        if (!*value) return E_OUTOFMEMORY;
        if (selected) {
            LONG position = 0;
            IUnknown* item = root_.ItemProviderSimple(*this, *selected);
            const HRESULT result = SafeArrayPutElement(*value, &position, item);
            if (FAILED(result)) {
                SafeArrayDestroy(*value);
                *value = nullptr;
                return result;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = component_->automation_selection_required() ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (horizontal != ScrollAmount_NoAmount) return E_INVALIDARG;
        if (vertical == ScrollAmount_NoAmount) return S_OK;
        return component_->RequestAutomationScrollVertical(ConvertScrollAmount(vertical))
                   ? S_OK
                   : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (horizontal != UIA_ScrollPatternNoScroll) return E_INVALIDARG;
        if (vertical == UIA_ScrollPatternNoScroll) return S_OK;
        return component_->RequestAutomationSetVerticalScrollPercent(vertical)
                   ? S_OK
                   : UIA_E_INVALIDOPERATION;
    }

    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) override {
        return ScrollBoolean(value, &components::AutomationScrollState::horizontally_scrollable);
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) override {
        return ScrollDouble(value, &components::AutomationScrollState::horizontal_scroll_percent);
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) override {
        return ScrollDouble(value, &components::AutomationScrollState::horizontal_view_size);
    }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) override {
        return ScrollBoolean(value, &components::AutomationScrollState::vertically_scrollable);
    }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) override {
        return ScrollDouble(value, &components::AutomationScrollState::vertical_scroll_percent);
    }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) override {
        return ScrollDouble(value, &components::AutomationScrollState::vertical_view_size);
    }

    HRESULT STDMETHODCALLTYPE SetVisualState(WindowVisualState state) override {
        return state == WindowVisualState_Normal ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        return component_->RequestAutomationClose() ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE WaitForInputIdle(int milliseconds, BOOL* value) override {
        if (!value) return E_POINTER;
        *value = TRUE;
        (void)milliseconds;
        return component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_CanMaximize(BOOL* value) override {
        return FixedBoolean(value, FALSE);
    }
    HRESULT STDMETHODCALLTYPE get_CanMinimize(BOOL* value) override {
        return FixedBoolean(value, FALSE);
    }
    HRESULT STDMETHODCALLTYPE get_IsModal(BOOL* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = component_->automation_is_modal() ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsTopmost(BOOL* value) override {
        return FixedBoolean(value, FALSE);
    }
    HRESULT STDMETHODCALLTYPE get_WindowInteractionState(WindowInteractionState* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = WindowInteractionState_ReadyForUserInteraction;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_WindowVisualState(WindowVisualState* value) override {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = WindowVisualState_Normal;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Move(double, double) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE Resize(double, double) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE Rotate(double) override { return UIA_E_INVALIDOPERATION; }
    HRESULT STDMETHODCALLTYPE get_CanMove(BOOL* value) override { return FixedBoolean(value, FALSE); }
    HRESULT STDMETHODCALLTYPE get_CanResize(BOOL* value) override { return FixedBoolean(value, FALSE); }
    HRESULT STDMETHODCALLTYPE get_CanRotate(BOOL* value) override { return FixedBoolean(value, FALSE); }

private:
    HRESULT RangeMember(double* value, double components::AutomationRangeValue::*member) const {
        if (!value) return E_POINTER;
        const auto range = component_ ? component_->automation_range_value() : std::nullopt;
        if (!range) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (*range).*member;
        return S_OK;
    }

    HRESULT ScrollBoolean(bool* value, bool components::AutomationScrollState::*member) const = delete;
    HRESULT ScrollBoolean(BOOL* value, bool components::AutomationScrollState::*member) const {
        if (!value) return E_POINTER;
        const auto state = component_ ? component_->automation_scroll_state() : std::nullopt;
        if (!state) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = ((*state).*member) ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT ScrollDouble(double* value, double components::AutomationScrollState::*member) const {
        if (!value) return E_POINTER;
        const auto state = component_ ? component_->automation_scroll_state() : std::nullopt;
        if (!state) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = (*state).*member;
        return S_OK;
    }
    HRESULT FixedBoolean(BOOL* value, BOOL fixed) const {
        if (!value) return E_POINTER;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = fixed;
        return S_OK;
    }

    AutomationRootProvider& root_;
    components::Component* component_ = nullptr;
    std::size_t index_ = 0;
};

class AutomationRootProvider::VirtualItemProvider final : public IRawElementProviderSimple,
                                                           public IRawElementProviderFragment,
                                                           public ISelectionItemProvider,
                                                           public IVirtualizedItemProvider,
                                                           public IScrollItemProvider {
public:
    VirtualItemProvider(AutomationRootProvider& root, ElementProvider& parent,
                        std::size_t item_index) noexcept
        : root_(root), parent_(&parent), item_index_(item_index) {}

    ElementProvider* parent() const noexcept { return parent_; }
    std::size_t item_index() const noexcept { return item_index_; }
    void Disconnect() noexcept { parent_ = nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == IID_IRawElementProviderFragment) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (iid == IID_ISelectionItemProvider) {
            *object = static_cast<ISelectionItemProvider*>(this);
        } else if (iid == IID_IVirtualizedItemProvider && !Realized()) {
            *object = static_cast<IVirtualizedItemProvider*>(this);
        } else if (iid == IID_IScrollItemProvider) {
            *object = static_cast<IScrollItemProvider*>(this);
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
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!Component()) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_SelectionItemPatternId) {
            return QueryInterface(IID_ISelectionItemProvider, reinterpret_cast<void**>(value));
        }
        if (id == UIA_VirtualizedItemPatternId && !Realized()) {
            return QueryInterface(IID_IVirtualizedItemProvider, reinterpret_cast<void**>(value));
        }
        if (id == UIA_ScrollItemPatternId) {
            return QueryInterface(IID_IScrollItemProvider, reinterpret_cast<void**>(value));
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        components::Component* component = Component();
        if (!component) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = UIA_ListItemControlTypeId;
        } else if (id == UIA_NamePropertyId) {
            return SetString(value, component->automation_item_name(item_index_));
        } else if (id == UIA_AutomationIdPropertyId) {
            return SetString(value, components::Utf8ToWide(component->definition().id) +
                                        L"-item-" + std::to_wstring(item_index_));
        } else if (id == UIA_ClassNamePropertyId) {
            return SetString(value, L"Terminal.VirtualListItem");
        } else if (id == UIA_FrameworkIdPropertyId) {
            return SetString(value, L"Terminal.Win32");
        } else if (id == UIA_IsEnabledPropertyId) {
            SetBoolean(value, component->enabled() && root_.IsReachable(component));
        } else if (id == UIA_IsKeyboardFocusablePropertyId) {
            SetBoolean(value, true);
        } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId) {
            SetBoolean(value, true);
        } else if (id == UIA_IsOffscreenPropertyId) {
            SetBoolean(value, !component->automation_item_screen_bounds(item_index_).has_value());
        } else if (id == UIA_IsSelectionItemPatternAvailablePropertyId ||
                   id == UIA_IsScrollItemPatternAvailablePropertyId) {
            SetBoolean(value, true);
        } else if (id == UIA_IsVirtualizedItemPatternAvailablePropertyId) {
            SetBoolean(value, !Realized());
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return Component() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        components::Component* component = Component();
        if (!component) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_Parent) {
            *value = static_cast<IRawElementProviderFragment*>(parent_);
        } else if (direction == NavigateDirection_PreviousSibling) {
            for (std::size_t index = item_index_; index > 0; --index) {
                if (component->automation_item_realized(index - 1)) {
                    *value = root_.ItemProviderFragment(*parent_, index - 1);
                    break;
                }
            }
        } else if (direction == NavigateDirection_NextSibling) {
            for (std::size_t index = item_index_ + 1;
                 index < component->automation_item_count(); ++index) {
                if (component->automation_item_realized(index)) {
                    *value = root_.ItemProviderFragment(*parent_, index);
                    break;
                }
            }
        }
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = SafeArrayCreateVector(VT_I4, 0, 3);
        if (!*value) return E_OUTOFMEMORY;
        LONG position = 0;
        LONG item = UiaAppendRuntimeId;
        SafeArrayPutElement(*value, &position, &item);
        position = 1;
        item = static_cast<LONG>(parent_ ? parent_->index() + 1 : 0);
        SafeArrayPutElement(*value, &position, &item);
        position = 2;
        item = static_cast<LONG>(item_index_ + 1);
        SafeArrayPutElement(*value, &position, &item);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        components::Component* component = Component();
        if (!component) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto bounds = component->automation_item_screen_bounds(item_index_);
        if (!bounds) {
            *value = {};
            return S_OK;
        }
        *value = UiaRect{static_cast<double>(bounds->left), static_cast<double>(bounds->top),
                         static_cast<double>(bounds->right - bounds->left),
                         static_cast<double>(bounds->bottom - bounds->top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        components::Component* component = Component();
        if (!component) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!component->RequestAutomationRealizeItem(item_index_) ||
            !component->RequestAutomationSelectItem(item_index_)) return UIA_E_INVALIDOPERATION;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<IRawElementProviderFragmentRoot*>(&root_);
        root_.AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Select() override {
        components::Component* component = Component();
        return component && component->RequestAutomationSelectItem(item_index_)
                   ? S_OK
                   : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
        return IsSelected() ? UIA_E_INVALIDOPERATION : S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
        if (!value) return E_POINTER;
        if (!Component()) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = IsSelected() ? TRUE : FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = parent_ ? static_cast<IRawElementProviderSimple*>(parent_) : nullptr;
        if (*value) (*value)->AddRef();
        return parent_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Realize() override {
        components::Component* component = Component();
        return component && component->RequestAutomationRealizeItem(item_index_)
                   ? S_OK
                   : UIA_E_INVALIDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return Realize(); }

private:
    components::Component* Component() const noexcept {
        return parent_ ? parent_->component() : nullptr;
    }
    bool Realized() const noexcept {
        components::Component* component = Component();
        return component && component->automation_item_realized(item_index_);
    }
    bool IsSelected() const noexcept {
        components::Component* component = Component();
        return component && component->automation_item_selected(item_index_);
    }

    AutomationRootProvider& root_;
    ElementProvider* parent_ = nullptr;
    std::size_t item_index_ = 0;
};

class AutomationRootProvider::PopupProvider final : public IRawElementProviderSimple,
                                                     public IRawElementProviderFragment,
                                                     public IRawElementProviderFragmentRoot,
                                                     public ISelectionProvider {
private:
    class ItemProvider final : public IRawElementProviderSimple,
                               public IRawElementProviderFragment,
                               public ISelectionItemProvider,
                               public IVirtualizedItemProvider,
                               public IScrollItemProvider {
    public:
        ItemProvider(PopupProvider& popup, std::size_t index) noexcept
            : popup_(popup), index_(index) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
            if (!object) return E_POINTER;
            *object = nullptr;
            if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
                *object = static_cast<IRawElementProviderSimple*>(this);
            } else if (iid == IID_IRawElementProviderFragment) {
                *object = static_cast<IRawElementProviderFragment*>(this);
            } else if (iid == IID_ISelectionItemProvider) {
                *object = static_cast<ISelectionItemProvider*>(this);
            } else if (iid == IID_IVirtualizedItemProvider && !Realized()) {
                *object = static_cast<IVirtualizedItemProvider*>(this);
            } else if (iid == IID_IScrollItemProvider) {
                *object = static_cast<IScrollItemProvider*>(this);
            } else {
                return E_NOINTERFACE;
            }
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return popup_.AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override { return popup_.Release(); }
        HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
            if (!value) return E_POINTER;
            *value = ProviderOptions_ServerSideProvider;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            if (!popup_.component_) return UIA_E_ELEMENTNOTAVAILABLE;
            if (id == UIA_SelectionItemPatternId) {
                return QueryInterface(IID_ISelectionItemProvider, reinterpret_cast<void**>(value));
            }
            if (id == UIA_VirtualizedItemPatternId && !Realized()) {
                return QueryInterface(IID_IVirtualizedItemProvider, reinterpret_cast<void**>(value));
            }
            if (id == UIA_ScrollItemPatternId) {
                return QueryInterface(IID_IScrollItemProvider, reinterpret_cast<void**>(value));
            }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
            if (!value) return E_POINTER;
            VariantInit(value);
            if (!popup_.component_) return UIA_E_ELEMENTNOTAVAILABLE;
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4;
                value->lVal = UIA_ListItemControlTypeId;
            } else if (id == UIA_NamePropertyId) {
                return SetString(value, popup_.component_->automation_popup_item_name(index_));
            } else if (id == UIA_AutomationIdPropertyId) {
                return SetString(value, components::Utf8ToWide(popup_.component_->definition().id) +
                                            L"-popup-item-" + std::to_wstring(index_));
            } else if (id == UIA_ClassNamePropertyId) {
                return SetString(value, L"Terminal.ComboPopupItem");
            } else if (id == UIA_FrameworkIdPropertyId) {
                return SetString(value, L"Terminal.Win32");
            } else if (id == UIA_IsEnabledPropertyId) {
                SetBoolean(value, popup_.component_->enabled());
            } else if (id == UIA_IsControlElementPropertyId ||
                       id == UIA_IsContentElementPropertyId ||
                       id == UIA_IsSelectionItemPatternAvailablePropertyId ||
                       id == UIA_IsScrollItemPatternAvailablePropertyId) {
                SetBoolean(value, true);
            } else if (id == UIA_IsOffscreenPropertyId) {
                SetBoolean(value, !Realized());
            } else if (id == UIA_IsVirtualizedItemPatternAvailablePropertyId) {
                SetBoolean(value, !Realized());
            }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
            IRawElementProviderSimple** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            return popup_.component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
        }
        HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                           IRawElementProviderFragment** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            if (!popup_.component_) return UIA_E_ELEMENTNOTAVAILABLE;
            if (direction == NavigateDirection_Parent) {
                *value = static_cast<IRawElementProviderFragment*>(&popup_);
            } else if (direction == NavigateDirection_PreviousSibling) {
                for (std::size_t item = index_; item > 0; --item) {
                    if (popup_.component_->automation_popup_item_realized(item - 1)) {
                        *value = static_cast<IRawElementProviderFragment*>(popup_.Item(item - 1));
                        break;
                    }
                }
            } else if (direction == NavigateDirection_NextSibling) {
                for (std::size_t item = index_ + 1;
                     item < popup_.component_->automation_popup_item_count(); ++item) {
                    if (popup_.component_->automation_popup_item_realized(item)) {
                        *value = static_cast<IRawElementProviderFragment*>(popup_.Item(item));
                        break;
                    }
                }
            }
            if (*value) (*value)->AddRef();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
            if (!value) return E_POINTER;
            *value = SafeArrayCreateVector(VT_I4, 0, 3);
            if (!*value) return E_OUTOFMEMORY;
            LONG position = 0;
            LONG item = UiaAppendRuntimeId;
            SafeArrayPutElement(*value, &position, &item);
            position = 1;
            item = static_cast<LONG>(popup_.parent_ ? popup_.parent_->index() + 1 : 0);
            SafeArrayPutElement(*value, &position, &item);
            position = 2;
            item = static_cast<LONG>(index_ + 1);
            SafeArrayPutElement(*value, &position, &item);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
            if (!value) return E_POINTER;
            if (!popup_.component_) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto bounds = popup_.component_->automation_popup_item_screen_bounds(index_);
            if (!bounds) {
                *value = {};
                return S_OK;
            }
            *value = UiaRect{static_cast<double>(bounds->left), static_cast<double>(bounds->top),
                             static_cast<double>(bounds->right - bounds->left),
                             static_cast<double>(bounds->bottom - bounds->top)};
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
            if (!value) return E_POINTER;
            *value = nullptr;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE SetFocus() override {
            if (!popup_.component_ ||
                !popup_.component_->RequestAutomationFocus() ||
                !popup_.component_->RequestAutomationRealizePopupItem(index_)) {
                return UIA_E_INVALIDOPERATION;
            }
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_FragmentRoot(
            IRawElementProviderFragmentRoot** value) override {
            if (!value) return E_POINTER;
            *value = static_cast<IRawElementProviderFragmentRoot*>(&popup_);
            popup_.AddRef();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE Select() override {
            return popup_.component_ && popup_.component_->RequestAutomationSelectPopupItem(index_)
                       ? S_OK
                       : UIA_E_INVALIDOPERATION;
        }
        HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
        HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
            return Selected() ? UIA_E_INVALIDOPERATION : S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
            if (!value) return E_POINTER;
            if (!popup_.component_) return UIA_E_ELEMENTNOTAVAILABLE;
            *value = Selected() ? TRUE : FALSE;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE get_SelectionContainer(
            IRawElementProviderSimple** value) override {
            if (!value) return E_POINTER;
            *value = static_cast<IRawElementProviderSimple*>(&popup_);
            popup_.AddRef();
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE Realize() override {
            return popup_.component_ && popup_.component_->RequestAutomationRealizePopupItem(index_)
                       ? S_OK
                       : UIA_E_INVALIDOPERATION;
        }
        HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return Realize(); }

    private:
        bool Realized() const noexcept {
            return popup_.component_ &&
                   popup_.component_->automation_popup_item_realized(index_);
        }
        bool Selected() const noexcept {
            return popup_.component_ &&
                   popup_.component_->automation_popup_item_selected(index_);
        }
        PopupProvider& popup_;
        std::size_t index_ = 0;
    };

public:
    PopupProvider(AutomationRootProvider& root, ElementProvider& parent,
                  components::Component& component) noexcept
        : root_(root), parent_(&parent), component_(&component) {}
    void SetWindow(HWND window) noexcept { window_ = window; }
    components::Component* component() const noexcept { return component_; }
    void Disconnect() noexcept {
        if (window_) UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(this));
        window_ = nullptr;
        component_ = nullptr;
        parent_ = nullptr;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IRawElementProviderSimple) {
            *object = static_cast<IRawElementProviderSimple*>(this);
        } else if (iid == IID_IRawElementProviderFragment) {
            *object = static_cast<IRawElementProviderFragment*>(this);
        } else if (iid == IID_IRawElementProviderFragmentRoot) {
            *object = static_cast<IRawElementProviderFragmentRoot*>(this);
        } else if (iid == IID_ISelectionProvider) {
            *object = static_cast<ISelectionProvider*>(this);
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
        *value = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return id == UIA_SelectionPatternId
                   ? QueryInterface(IID_ISelectionProvider, reinterpret_cast<void**>(value))
                   : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_ControlTypePropertyId) {
            value->vt = VT_I4;
            value->lVal = UIA_ListControlTypeId;
        } else if (id == UIA_NamePropertyId) {
            return SetString(value, component_->automation_name() + L" options");
        } else if (id == UIA_AutomationIdPropertyId) {
            return SetString(value, components::Utf8ToWide(component_->definition().id) + L"-popup");
        } else if (id == UIA_ClassNamePropertyId) {
            return SetString(value, L"Terminal.ComboPopup");
        } else if (id == UIA_FrameworkIdPropertyId) {
            return SetString(value, L"Terminal.Win32");
        } else if (id == UIA_IsEnabledPropertyId || id == UIA_IsControlElementPropertyId ||
                   id == UIA_IsContentElementPropertyId ||
                   id == UIA_IsSelectionPatternAvailablePropertyId) {
            SetBoolean(value, true);
        } else if (id == UIA_IsOffscreenPropertyId) {
            SetBoolean(value, !component_->automation_popup_visible());
        } else if (id == UIA_NativeWindowHandlePropertyId && window_) {
            value->vt = VT_I4;
            value->lVal = HandleToLong(window_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return window_ ? UiaHostProviderFromHwnd(window_, value) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        EnsureItems();
        if (direction == NavigateDirection_Parent && parent_) {
            *value = static_cast<IRawElementProviderFragment*>(parent_);
        } else if (direction == NavigateDirection_FirstChild) {
            for (std::size_t index = 0; index < items_.size(); ++index) {
                if (component_->automation_popup_item_realized(index)) {
                    *value = static_cast<IRawElementProviderFragment*>(items_[index].get());
                    break;
                }
            }
        } else if (direction == NavigateDirection_LastChild) {
            for (std::size_t index = items_.size(); index > 0; --index) {
                if (component_->automation_popup_item_realized(index - 1)) {
                    *value = static_cast<IRawElementProviderFragment*>(items_[index - 1].get());
                    break;
                }
            }
        }
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        if (!window_) return UIA_E_ELEMENTNOTAVAILABLE;
        RECT bounds{};
        GetWindowRect(window_, &bounds);
        *value = UiaRect{static_cast<double>(bounds.left), static_cast<double>(bounds.top),
                         static_cast<double>(bounds.right - bounds.left),
                         static_cast<double>(bounds.bottom - bounds.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return parent_ ? parent_->SetFocus() : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        EnsureItems();
        for (std::size_t index = 0; index < items_.size(); ++index) {
            const auto bounds = component_->automation_popup_item_screen_bounds(index);
            if (bounds && x >= bounds->left && x < bounds->right && y >= bounds->top &&
                y < bounds->bottom) {
                *value = static_cast<IRawElementProviderFragment*>(items_[index].get());
                (*value)->AddRef();
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        EnsureItems();
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (component_->automation_popup_item_selected(index)) {
                *value = static_cast<IRawElementProviderFragment*>(items_[index].get());
                (*value)->AddRef();
                break;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!component_) return UIA_E_ELEMENTNOTAVAILABLE;
        EnsureItems();
        ItemProvider* selected = nullptr;
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (component_->automation_popup_item_selected(index)) {
                selected = items_[index].get();
                break;
            }
        }
        *value = SafeArrayCreateVector(VT_UNKNOWN, 0, selected ? 1 : 0);
        if (!*value) return E_OUTOFMEMORY;
        if (selected) {
            LONG position = 0;
            IUnknown* item = static_cast<IRawElementProviderSimple*>(selected);
            const HRESULT result = SafeArrayPutElement(*value, &position, item);
            if (FAILED(result)) {
                SafeArrayDestroy(*value);
                *value = nullptr;
                return result;
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return component_ ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

private:
    void EnsureItems() {
        if (!component_) return;
        while (items_.size() < component_->automation_popup_item_count()) {
            items_.push_back(std::make_unique<ItemProvider>(*this, items_.size()));
        }
    }
    ItemProvider* Item(std::size_t index) {
        EnsureItems();
        return index < items_.size() ? items_[index].get() : nullptr;
    }

    AutomationRootProvider& root_;
    ElementProvider* parent_ = nullptr;
    components::Component* component_ = nullptr;
    HWND window_ = nullptr;
    std::vector<std::unique_ptr<ItemProvider>> items_;
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
    for (const auto& popup : popups_) popup->Disconnect();
    for (const auto& item : virtual_items_) item->Disconnect();
    for (const auto& element : elements_) element->Disconnect();
    window_ = nullptr;
    component_root_ = nullptr;
    focused_component_ = {};
    active_scope_ = nullptr;
}

void AutomationRootProvider::SetActiveScope(components::Component* scope) {
    if (active_scope_ == scope) return;
    active_scope_ = scope;
    if (window_) {
        UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(this),
                                      StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
    NotifyFocusChanged();
}

void AutomationRootProvider::NotifyFocusChanged() {
    if (!window_ || !focused_component_) return;
    ElementProvider* provider = FindProvider(focused_component_());
    if (provider && IsReachable(provider->component())) {
        UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider),
                                UIA_AutomationFocusChangedEventId);
    }
}

void AutomationRootProvider::NotifyPopupStateChanged(components::Component* component, bool open) {
    ElementProvider* provider = FindProvider(component);
    if (!window_ || !provider) return;
    if (open && !FindPopupProvider(component)) {
        popups_.push_back(std::make_unique<PopupProvider>(*this, *provider, *component));
        popups_.back()->SetWindow(component->automation_popup_hwnd());
    }
    VARIANT old_value;
    VARIANT new_value;
    VariantInit(&old_value);
    VariantInit(&new_value);
    old_value.vt = VT_I4;
    new_value.vt = VT_I4;
    old_value.lVal = open ? ExpandCollapseState_Collapsed : ExpandCollapseState_Expanded;
    new_value.lVal = open ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    UiaRaiseAutomationPropertyChangedEvent(static_cast<IRawElementProviderSimple*>(provider),
                                           UIA_ExpandCollapseExpandCollapseStatePropertyId,
                                           old_value, new_value);
    UiaRaiseStructureChangedEvent(static_cast<IRawElementProviderSimple*>(provider),
                                  StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

LRESULT AutomationRootProvider::ReturnPopupProvider(components::Component* component, HWND popup,
                                                     WPARAM wparam, LPARAM lparam) {
    if (!window_ || !component || !popup) return 0;
    PopupProvider* provider = FindPopupProvider(component);
    if (!provider) {
        ElementProvider* parent = FindProvider(component);
        if (!parent) return 0;
        popups_.push_back(std::make_unique<PopupProvider>(*this, *parent, *component));
        provider = popups_.back().get();
    }
    provider->SetWindow(popup);
    return UiaReturnRawElementProvider(
        popup, wparam, lparam, static_cast<IRawElementProviderSimple*>(provider));
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
    const std::vector<ElementProvider*> children = AccessibleChildren(nullptr);
    if (direction == NavigateDirection_FirstChild && !children.empty()) {
        *value = static_cast<IRawElementProviderFragment*>(children.front());
    } else if (direction == NavigateDirection_LastChild && !children.empty()) {
        *value = static_cast<IRawElementProviderFragment*>(children.back());
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
        if (!IsReachable((*item)->component())) continue;
        if ((*item)->component()->automation_supports_item_container()) {
            for (std::size_t index = 0;
                 index < (*item)->component()->automation_item_count(); ++index) {
                const auto item_bounds =
                    (*item)->component()->automation_item_screen_bounds(index);
                if (item_bounds && x >= item_bounds->left && x < item_bounds->right &&
                    y >= item_bounds->top && y < item_bounds->bottom) {
                    *value = ItemProviderFragment(*(*item), index);
                    (*value)->AddRef();
                    return S_OK;
                }
            }
        }
        const UiaRect bounds = ScreenBounds(window_, (*item)->component()->automation_bounds());
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
    if (provider && IsReachable(provider->component())) {
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

AutomationRootProvider::VirtualItemProvider* AutomationRootProvider::ItemProvider(
    ElementProvider& parent, std::size_t index) {
    const auto found = std::find_if(
        virtual_items_.begin(), virtual_items_.end(),
        [&parent, index](const auto& item) {
            return item->parent() == &parent && item->item_index() == index;
        });
    if (found != virtual_items_.end()) return found->get();
    virtual_items_.push_back(std::make_unique<VirtualItemProvider>(*this, parent, index));
    return virtual_items_.back().get();
}

IRawElementProviderSimple* AutomationRootProvider::ItemProviderSimple(
    ElementProvider& parent, std::size_t index) {
    return static_cast<IRawElementProviderSimple*>(ItemProvider(parent, index));
}

IRawElementProviderFragment* AutomationRootProvider::ItemProviderFragment(
    ElementProvider& parent, std::size_t index) {
    return static_cast<IRawElementProviderFragment*>(ItemProvider(parent, index));
}

AutomationRootProvider::PopupProvider* AutomationRootProvider::FindPopupProvider(
    const components::Component* component) const noexcept {
    const auto found = std::find_if(popups_.begin(), popups_.end(),
                                    [component](const auto& popup) {
                                        return popup->component() == component;
                                    });
    return found == popups_.end() ? nullptr : found->get();
}

IRawElementProviderFragment* AutomationRootProvider::PopupProviderFragment(
    const components::Component* component) const noexcept {
    PopupProvider* provider = FindPopupProvider(component);
    return provider ? static_cast<IRawElementProviderFragment*>(provider) : nullptr;
}

bool AutomationRootProvider::IsReachable(const components::Component* component) const noexcept {
    if (!component) return false;
    if (active_scope_) return component->IsDescendantOrSelfOf(active_scope_);
    for (const components::Component* current = component; current; current = current->parent()) {
        if (current->IsModalOverlay() && !current->IsModalActive()) return false;
    }
    return true;
}

std::vector<AutomationRootProvider::ElementProvider*>
AutomationRootProvider::ReachableElements() const {
    std::vector<ElementProvider*> reachable;
    reachable.reserve(elements_.size());
    for (const auto& element : elements_) {
        if (IsReachable(element->component())) reachable.push_back(element.get());
    }
    return reachable;
}

AutomationRootProvider::ElementProvider* AutomationRootProvider::AccessibleParent(
    const ElementProvider* provider) const noexcept {
    if (!provider || !provider->component()) return nullptr;
    for (const components::Component* current = provider->component()->parent(); current;
         current = current->parent()) {
        ElementProvider* candidate = FindProvider(current);
        if (candidate && IsReachable(candidate->component())) return candidate;
    }
    return nullptr;
}

std::vector<AutomationRootProvider::ElementProvider*>
AutomationRootProvider::AccessibleChildren(const ElementProvider* parent) const {
    std::vector<ElementProvider*> children;
    for (const auto& element : elements_) {
        if (!IsReachable(element->component()) || element.get() == parent) continue;
        if (AccessibleParent(element.get()) == parent) children.push_back(element.get());
    }
    return children;
}

}  // namespace ui::accessibility
