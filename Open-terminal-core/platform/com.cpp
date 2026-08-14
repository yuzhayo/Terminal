#include "platform/com.h"

#include <windows.h>
#include <objbase.h>

namespace platform {
namespace {

bool g_initialized = false;

}  // namespace

bool EnsureCom() {
    if (g_initialized) return true;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // RPC_E_CHANGED_MODE means someone already initialized this thread with a
    // different model; the apartment is usable either way.
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
    g_initialized = true;
    return true;
}

void ReleaseCom() {
    if (!g_initialized) return;
    g_initialized = false;
    CoUninitialize();
}

}  // namespace platform
