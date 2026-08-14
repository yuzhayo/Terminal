#pragma once

#include <windows.h>
#include <ole2.h>
#include <oleacc.h>
#include <UIAutomation.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace ui::components {
class Component;
}

namespace ui::accessibility {

class AutomationRootProvider final : public IRawElementProviderSimple,
                                     public IRawElementProviderFragment,
                                     public IRawElementProviderFragmentRoot,
                                     public IRawElementProviderHwndOverride {
public:
    AutomationRootProvider(HWND window, components::Component& root,
                           std::function<components::Component*()> focused_component);
    ~AutomationRootProvider();

    void Disconnect() noexcept;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern_id, IUnknown** value) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property_id, VARIANT* value) override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
        IRawElementProviderSimple** value) override;

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** value) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override;

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** value) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override;
    HRESULT STDMETHODCALLTYPE GetOverrideProviderForHwnd(
        HWND hwnd, IRawElementProviderSimple** value) override;

private:
    class ElementProvider;
    ElementProvider* FindProvider(const components::Component* component) const noexcept;
    ElementProvider* FindProvider(HWND hwnd) const noexcept;
    ElementProvider* ProviderAt(std::size_t index) const noexcept;

    std::atomic_ulong references_{1};
    HWND window_ = nullptr;
    components::Component* component_root_ = nullptr;
    std::function<components::Component*()> focused_component_;
    std::vector<std::unique_ptr<ElementProvider>> elements_;
};

}  // namespace ui::accessibility
