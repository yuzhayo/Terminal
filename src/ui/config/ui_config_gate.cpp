#include "ui/config/ui_config_gate.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "resource.h"

namespace ui::config {
namespace {

constexpr std::uintmax_t kMaximumDocumentBytes = 4U * 1024U * 1024U;
constexpr std::uintmax_t kMaximumLogBytes = 1024U * 1024U;
constexpr wchar_t kEmbeddedSource[] = L"Assets\\ui\\core.json + Assets\\ui\\screens\\*.json";

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) return L"Detail error tidak dapat dikonversi ke Unicode.";
    std::wstring converted(static_cast<std::size_t>(required), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), converted.data(), required);
    return written == required ? converted : L"Detail error tidak dapat dikonversi ke Unicode.";
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) return "<unicode-conversion-failed>";
    std::string converted(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), converted.data(), required,
                                            nullptr, nullptr);
    return written == required ? converted : "<unicode-conversion-failed>";
}

std::wstring FormatDiagnostic(const ResolveDiagnostic& diagnostic) {
    return L"Override UI tidak diterapkan.\n\nCode: " + Utf8ToWide(diagnostic.code) +
           L"\nSource: " + Utf8ToWide(diagnostic.source) + L"\nPath: " +
           Utf8ToWide(diagnostic.path) + L"\nError: " + Utf8ToWide(diagnostic.message);
}

std::string LoadEmbedded(HINSTANCE instance) {
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(IDR_UI_DEFAULT_JSON), RT_RCDATA);
    if (!resource) throw std::runtime_error("Embedded UI default tidak ditemukan.");
    const DWORD size = SizeofResource(instance, resource);
    if (size == 0 || size > kMaximumDocumentBytes) {
        throw std::runtime_error("Ukuran embedded UI default tidak valid.");
    }
    const HGLOBAL loaded = LoadResource(instance, resource);
    const auto* bytes = loaded ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (!bytes) throw std::runtime_error("Embedded UI default tidak dapat dibaca.");
    return std::string(bytes, bytes + size);
}

enum class OverrideReadStatus { Missing, Loaded, Failed };

struct OverrideReadResult {
    OverrideReadStatus status = OverrideReadStatus::Missing;
    std::string bytes;
    ResolveDiagnostic diagnostic;
};

OverrideReadResult ReadOverride(const std::wstring& path) {
    std::error_code error;
    const std::filesystem::path file(path);
    if (!std::filesystem::exists(file, error)) {
        if (!error) return {};
        return {OverrideReadStatus::Failed, {},
                {"override-stat", WideToUtf8(path), "", error.message(), false}};
    }
    if (!std::filesystem::is_regular_file(file, error) || error) {
        return {OverrideReadStatus::Failed, {},
                {"override-file", WideToUtf8(path), "", "Override path is not a regular file.", false}};
    }
    const std::uintmax_t size = std::filesystem::file_size(file, error);
    if (error || size == 0 || size > kMaximumDocumentBytes) {
        return {OverrideReadStatus::Failed, {},
                {"document-size", WideToUtf8(path), "", "Override size must be between 1 byte and 4 MiB.", false}};
    }
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        return {OverrideReadStatus::Failed, {},
                {"override-read", WideToUtf8(path), "", "Override could not be opened.", false}};
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() != size) {
        return {OverrideReadStatus::Failed, {},
                {"override-read", WideToUtf8(path), "", "Override read was incomplete.", false}};
    }
    return {OverrideReadStatus::Loaded, std::move(bytes), {}};
}

}  // namespace

UiConfigGate::UiConfigGate(HINSTANCE instance, platform::AppPaths paths)
    : instance_(instance), paths_(std::move(paths)) {}

bool UiConfigGate::ResolveBootstrap(std::wstring& diagnostic) {
    return ResolveCandidate(true, diagnostic);
}

bool UiConfigGate::Reload(std::wstring& diagnostic) {
    return ResolveCandidate(false, diagnostic);
}

bool UiConfigGate::ResolveCandidate(bool bootstrap, std::wstring& diagnostic) {
    try {
        const std::string embedded = LoadEmbedded(instance_);
        const OverrideReadResult override_result = ReadOverride(paths_.ui_override);
        if (override_result.status == OverrideReadStatus::Failed) {
            if (!bootstrap && document_) {
                active_diagnostic_ = override_result.diagnostic;
                RecordDiagnostic(*active_diagnostic_);
                diagnostic = FormatDiagnostic(*active_diagnostic_);
                return false;
            }
            auto resolved = detail::ResolveDocuments(embedded, std::nullopt,
                                                     document_ ? document_->generation + 1 : 1);
            resolved.override_diagnostic = override_result.diagnostic;
            Publish(std::move(resolved.document), std::move(resolved.override_diagnostic));
            diagnostic.clear();
            return true;
        }

        const std::optional<std::string_view> override_text =
            override_result.status == OverrideReadStatus::Loaded
                ? std::optional<std::string_view>(override_result.bytes)
                : std::nullopt;
        auto resolved = detail::ResolveDocuments(embedded, override_text,
                                                 document_ ? document_->generation + 1 : 1);
        if (resolved.override_diagnostic) {
            resolved.override_diagnostic->source = WideToUtf8(paths_.ui_override);
        }
        if (!bootstrap && resolved.override_diagnostic && document_) {
            active_diagnostic_ = resolved.override_diagnostic;
            RecordDiagnostic(*active_diagnostic_);
            diagnostic = FormatDiagnostic(*active_diagnostic_);
            return false;
        }
        Publish(std::move(resolved.document), std::move(resolved.override_diagnostic));
        diagnostic.clear();
        return true;
    } catch (const std::exception& error) {
        diagnostic = std::wstring(L"Embedded UI default tidak valid.\n\nSource: ") +
                     kEmbeddedSource + L"\nError: " + Utf8ToWide(error.what());
        return false;
    }
}

void UiConfigGate::Publish(std::shared_ptr<const ResolvedUiDocument> document,
                           std::optional<ResolveDiagnostic> override_diagnostic) {
    document_ = std::move(document);
    active_diagnostic_ = std::move(override_diagnostic);
    if (active_diagnostic_) RecordDiagnostic(*active_diagnostic_);
}

void UiConfigGate::RecordDiagnostic(const ResolveDiagnostic& diagnostic) const noexcept {
    try {
        const std::filesystem::path log_path(paths_.ui_config_log);
        std::filesystem::create_directories(log_path.parent_path());
        std::error_code error;
        if (std::filesystem::exists(log_path, error) && !error &&
            std::filesystem::file_size(log_path, error) > kMaximumLogBytes && !error) {
            std::filesystem::remove(log_path, error);
        }
        std::ofstream output(log_path, std::ios::binary | std::ios::app);
        if (!output) return;
        SYSTEMTIME time{};
        GetSystemTime(&time);
        output << time.wYear << '-' << time.wMonth << '-' << time.wDay << 'T' << time.wHour << ':'
               << time.wMinute << ':' << time.wSecond << "Z code=" << diagnostic.code
               << " source=" << diagnostic.source << " path=" << diagnostic.path
               << " message=" << diagnostic.message << '\n';
    } catch (...) {
    }
}

std::shared_ptr<const ResolvedUiDocument> UiConfigGate::document() const noexcept {
    return document_;
}

const UiConfigMetadata& UiConfigGate::metadata() const noexcept {
    static const UiConfigMetadata empty;
    return document_ ? document_->metadata : empty;
}

const std::optional<ResolveDiagnostic>& UiConfigGate::active_diagnostic() const noexcept {
    return active_diagnostic_;
}

std::wstring UiConfigGate::active_diagnostic_text() const {
    return active_diagnostic_ ? FormatDiagnostic(*active_diagnostic_) : std::wstring{};
}

const platform::AppPaths& UiConfigGate::paths() const noexcept {
    return paths_;
}

}  // namespace ui::config
