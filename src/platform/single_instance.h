#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace platform {

inline constexpr ULONG_PTR kIpcCopyDataId = 0x594F544E00000001ull;
inline constexpr std::size_t kMaximumIpcPayloadBytes = 64u * 1024u;

const wchar_t* MainWindowClassName() noexcept;
const wchar_t* InfrastructureWindowClassName() noexcept;

enum class IpcCommand { ActivateDefault, OpenRoute, RequestExit };

struct IpcRequest {
    std::string request_id;
    IpcCommand command = IpcCommand::ActivateDefault;
    std::string route_id;

    bool operator==(const IpcRequest&) const = default;
};

enum class IpcStatus : WORD { Accepted = 1, Rejected = 2, Busy = 3 };
enum class IpcError : WORD {
    None = 0,
    InvalidPayload = 1,
    UnsupportedVersion = 2,
    UnsupportedCommand = 3,
    InvalidRoute = 4,
    QueueFull = 5,
    ShutdownInProgress = 6,
};

struct IpcParseResult {
    std::optional<IpcRequest> request;
    IpcError error = IpcError::InvalidPayload;
};

enum class InstanceClaim {
    Primary,
    SecondaryAccepted,
    SecondaryReceiverNotFound,
    SecondaryRejected,
    SecondaryBusy,
    SecondaryTimedOut,
    Error,
};

class SingleInstance final {
public:
    SingleInstance() = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    InstanceClaim Claim(const std::wstring& command_line);
    void Release();

private:
    HANDLE mutex_ = nullptr;
};

std::wstring BuildCurrentUserMutexName();
std::optional<IpcRequest> BuildIpcRequestFromCommandLine(const std::wstring& command_line);
std::string SerializeIpcRequest(const IpcRequest& request);
IpcParseResult ParseIpcPayload(const void* bytes, std::size_t byte_count);
LRESULT PackIpcResult(IpcStatus status, IpcError error) noexcept;
void ActivateMainWindow(HWND window);

}  // namespace platform
