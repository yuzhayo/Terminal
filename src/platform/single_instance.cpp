#include "platform/single_instance.h"

#include <sddl.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace platform {
namespace {

using Json = nlohmann::json;

constexpr wchar_t kMainWindowClass[] = L"Terminal.MainWindow";
constexpr wchar_t kInfrastructureClass[] = L"Yuzha.Terminal.Infrastructure.v1";
constexpr wchar_t kMutexPrefix[] = L"Local\\Yuzha.Terminal.Instance.v1.";
constexpr UINT kSendTimeoutMs = 1000;
constexpr std::array<DWORD, 5> kReceiverScheduleMs{0, 50, 150, 350, 750};
bool IsRoute(std::string_view route) noexcept {
    if (route.empty() || route.size() > 128 || route.front() == '-' || route.back() == '-') {
        return false;
    }
    bool previous_hyphen = false;
    for (const unsigned char character : route) {
        const bool hyphen = character == '-';
        if (!(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && !hyphen) {
            return false;
        }
        if (hyphen && previous_hyphen) return false;
        previous_hyphen = hyphen;
    }
    return true;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

std::string CreateRequestId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) return {};
    wchar_t text[40]{};
    if (StringFromGUID2(guid, text, static_cast<int>(std::size(text))) <= 0) return {};
    std::wstring value(text);
    if (value.size() != 38 || value.front() != L'{' || value.back() != L'}') return {};
    value = value.substr(1, 36);
    std::string result = WideToUtf8(value);
    for (char& ch : result) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

bool IsLowercaseUuid(std::string_view value) noexcept {
    if (value.size() != 36) return false;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!((value[index] >= '0' && value[index] <= '9') ||
                     (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> CurrentUserSidText() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return std::nullopt;
    DWORD required = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &required);
    std::vector<std::byte> storage(required);
    if (required == 0 || !GetTokenInformation(token, TokenUser, storage.data(), required, &required)) {
        CloseHandle(token);
        return std::nullopt;
    }
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(storage.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(token_user->User.Sid, &sid_text)) {
        CloseHandle(token);
        return std::nullopt;
    }
    const std::string result = WideToUtf8(sid_text);
    LocalFree(sid_text);
    CloseHandle(token);
    return result.empty() ? std::nullopt : std::optional<std::string>(result);
}

std::optional<std::array<BYTE, 32>> Sha256(std::string_view value) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return std::nullopt;
    }
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) ||
        !CryptHashData(hash, reinterpret_cast<const BYTE*>(value.data()),
                       static_cast<DWORD>(value.size()), 0)) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return std::nullopt;
    }
    std::array<BYTE, 32> digest{};
    DWORD size = static_cast<DWORD>(digest.size());
    const bool success = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &size, 0) != FALSE &&
                         size == digest.size();
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return success ? std::optional<std::array<BYTE, 32>>(digest) : std::nullopt;
}

InstanceClaim ForwardRequest(const IpcRequest& request) {
    const std::string payload = SerializeIpcRequest(request);
    if (payload.empty() || payload.size() > kMaximumIpcPayloadBytes) return InstanceClaim::Error;
    COPYDATASTRUCT copy_data{kIpcCopyDataId, static_cast<DWORD>(payload.size()),
                             const_cast<char*>(payload.data())};
    const ULONGLONG started = GetTickCount64();
    for (const DWORD scheduled : kReceiverScheduleMs) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed < scheduled) Sleep(static_cast<DWORD>(scheduled - elapsed));
        HWND receiver = FindWindowW(kInfrastructureClass, nullptr);
        if (!receiver) continue;
        DWORD_PTR packed = 0;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(receiver, WM_COPYDATA, 0,
                                 reinterpret_cast<LPARAM>(&copy_data),
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, kSendTimeoutMs, &packed)) {
            if (GetLastError() == ERROR_TIMEOUT) {
                Sleep(250);
                if (!SendMessageTimeoutW(receiver, WM_COPYDATA, 0,
                                         reinterpret_cast<LPARAM>(&copy_data),
                                         SMTO_ABORTIFHUNG | SMTO_BLOCK, kSendTimeoutMs, &packed)) {
                    return InstanceClaim::SecondaryTimedOut;
                }
            } else {
                continue;
            }
        }
        const auto status = static_cast<IpcStatus>(LOWORD(packed));
        if (status == IpcStatus::Accepted) return InstanceClaim::SecondaryAccepted;
        if (status == IpcStatus::Busy) return InstanceClaim::SecondaryBusy;
        return InstanceClaim::SecondaryRejected;
    }
    return InstanceClaim::SecondaryReceiverNotFound;
}

}  // namespace

const wchar_t* MainWindowClassName() noexcept { return kMainWindowClass; }
const wchar_t* InfrastructureWindowClassName() noexcept { return kInfrastructureClass; }

SingleInstance::~SingleInstance() { Release(); }

InstanceClaim SingleInstance::Claim(const std::wstring& command_line) {
    const std::wstring mutex_name = BuildCurrentUserMutexName();
    if (mutex_name.empty()) return InstanceClaim::Error;
    mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (!mutex_) return InstanceClaim::Error;
    if (GetLastError() != ERROR_ALREADY_EXISTS) return InstanceClaim::Primary;
    Release();
    const std::optional<IpcRequest> request = BuildIpcRequestFromCommandLine(command_line);
    return request ? ForwardRequest(*request) : InstanceClaim::Error;
}

void SingleInstance::Release() {
    if (mutex_) CloseHandle(mutex_);
    mutex_ = nullptr;
}

std::wstring BuildCurrentUserMutexName() {
    const auto sid = CurrentUserSidText();
    if (!sid) return {};
    const auto digest = Sha256(*sid);
    if (!digest) return {};
    std::wostringstream suffix;
    suffix << std::hex << std::setfill(L'0');
    for (std::size_t index = 0; index < 16; ++index) {
        suffix << std::setw(2) << static_cast<unsigned int>((*digest)[index]);
    }
    return std::wstring(kMutexPrefix) + suffix.str();
}

std::optional<IpcRequest> BuildIpcRequestFromCommandLine(const std::wstring& command_line) {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(command_line.c_str(), &count);
    if (!arguments) return std::nullopt;
    IpcRequest request;
    request.request_id = CreateRequestId();
    for (int index = 1; index < count; ++index) {
        if (_wcsicmp(arguments[index], L"--exit") == 0) {
            request.command = IpcCommand::RequestExit;
        } else if (_wcsicmp(arguments[index], L"--route") == 0 && index + 1 < count) {
            request.command = IpcCommand::OpenRoute;
            request.route_id = WideToUtf8(arguments[++index]);
        }
    }
    LocalFree(arguments);
    if (request.request_id.empty() ||
        (request.command == IpcCommand::OpenRoute && !IsRoute(request.route_id))) {
        return std::nullopt;
    }
    return request;
}

std::string SerializeIpcRequest(const IpcRequest& request) {
    if (!IsLowercaseUuid(request.request_id)) return {};
    Json arguments = Json::object();
    std::string command;
    switch (request.command) {
        case IpcCommand::ActivateDefault: command = "activate-default"; break;
        case IpcCommand::OpenRoute:
            if (!IsRoute(request.route_id)) return {};
            command = "open-route";
            arguments["routeId"] = request.route_id;
            break;
        case IpcCommand::RequestExit: command = "request-exit"; break;
    }
    return Json{{"protocol", "yuzha.terminal.ipc"}, {"version", 1},
                {"requestId", request.request_id}, {"command", command},
                {"arguments", std::move(arguments)}}.dump();
}

IpcParseResult ParseIpcPayload(const void* bytes, std::size_t byte_count) {
    if (!bytes || byte_count == 0 || byte_count > kMaximumIpcPayloadBytes) return {};
    const auto* first = static_cast<const char*>(bytes);
    if (std::find(first, first + byte_count, '\0') != first + byte_count) return {};
    bool valid_structure = true;
    std::map<int, std::set<std::string, std::less<>>> keys;
    const auto callback = [&valid_structure, &keys](int depth, Json::parse_event_t event,
                                                     Json& parsed) {
        if (depth > 16) valid_structure = false;
        if (event == Json::parse_event_t::object_start) keys[depth].clear();
        if (event == Json::parse_event_t::key) {
            const std::string key = parsed.get<std::string>();
            if (!keys[depth].insert(key).second) valid_structure = false;
        }
        if (event == Json::parse_event_t::object_end) keys.erase(depth);
        return valid_structure;
    };
    Json document;
    try {
        document = Json::parse(first, first + byte_count, callback, true, false);
    } catch (...) {
        return {};
    }
    if (!valid_structure || !document.is_object() || document.size() != 5 ||
        !document.contains("protocol") || !document.contains("version") ||
        !document.contains("requestId") || !document.contains("command") ||
        !document.contains("arguments") || !document["protocol"].is_string() ||
        !document["version"].is_number_integer() || !document["requestId"].is_string() ||
        !document["command"].is_string() || !document["arguments"].is_object()) {
        return {};
    }
    if (document["protocol"].get<std::string>() != "yuzha.terminal.ipc") return {};
    if (document["version"].get<int>() != 1) return {{}, IpcError::UnsupportedVersion};
    IpcRequest request;
    request.request_id = document["requestId"].get<std::string>();
    if (!IsLowercaseUuid(request.request_id)) return {};
    const std::string command = document["command"].get<std::string>();
    const Json& arguments = document["arguments"];
    if (command == "activate-default") {
        if (!arguments.empty()) return {};
        request.command = IpcCommand::ActivateDefault;
    } else if (command == "request-exit") {
        if (!arguments.empty()) return {};
        request.command = IpcCommand::RequestExit;
    } else if (command == "open-route") {
        if (arguments.size() != 1 || !arguments.contains("routeId") ||
            !arguments["routeId"].is_string()) return {{}, IpcError::InvalidRoute};
        request.route_id = arguments["routeId"].get<std::string>();
        if (!IsRoute(request.route_id)) return {{}, IpcError::InvalidRoute};
        request.command = IpcCommand::OpenRoute;
    } else {
        return {{}, IpcError::UnsupportedCommand};
    }
    return {request, IpcError::None};
}

LRESULT PackIpcResult(IpcStatus status, IpcError error) noexcept {
    return MAKELONG(static_cast<WORD>(status), static_cast<WORD>(error));
}

void ActivateMainWindow(HWND window) {
    if (!window) return;
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window);
}

}  // namespace platform
