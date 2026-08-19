#include "platform/jump_list.h"

#include <windows.h>

#include <objectarray.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>

#include "app/app_identity.h"
#include "logic/platform/com.h"

namespace platform {
namespace {

std::wstring ExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) == 0) return {};
    return buffer;
}

std::wstring ExecutableDir() {
    const std::wstring exe = ExecutablePath();
    const std::size_t slash = exe.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : exe.substr(0, slash);
}

// Satu task jump list: menjalankan exe ini dengan switch route/exit.
IShellLinkW* MakeTask(const std::wstring& title, const std::wstring& arguments) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        return nullptr;
    }
    const std::wstring exe = ExecutablePath();
    link->SetPath(exe.c_str());
    link->SetArguments(arguments.c_str());
    link->SetIconLocation(exe.c_str(), 0);
    link->SetWorkingDirectory(ExecutableDir().c_str());

    IPropertyStore* store = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&store)))) {
        PROPVARIANT value{};
        if (SUCCEEDED(InitPropVariantFromString(title.c_str(), &value))) {
            store->SetValue(PKEY_Title, value);
            store->Commit();
            PropVariantClear(&value);
        }
        store->Release();
    }
    return link;
}

}  // namespace

void ApplyAppUserModelId() { SetCurrentProcessExplicitAppUserModelID(app_identity::kApplicationId); }

void InstallJumpList(std::span<const JumpListRoute> routes) {
    if (!EnsureCom()) return;

    ICustomDestinationList* list = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&list)))) {
        return;
    }
    list->SetAppID(app_identity::kApplicationId);

    UINT slots = 0;
    IObjectArray* removed = nullptr;
    if (FAILED(list->BeginList(&slots, IID_PPV_ARGS(&removed)))) {
        list->Release();
        return;
    }

    IObjectCollection* tasks = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&tasks)))) {
        for (const JumpListRoute& route : routes) {
            if (route.title.empty() || route.route_id.empty()) continue;
            if (IShellLinkW* link = MakeTask(route.title, L"--route " + route.route_id)) {
                tasks->AddObject(link);
                link->Release();
            }
        }
        IObjectArray* array = nullptr;
        if (SUCCEEDED(tasks->QueryInterface(IID_PPV_ARGS(&array)))) {
            list->AddUserTasks(array);
            array->Release();
        }
        tasks->Release();
    }

    list->CommitList();
    if (removed) removed->Release();
    list->Release();
}

}  // namespace platform
