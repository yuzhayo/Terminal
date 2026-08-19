#pragma once

#include <span>
#include <string>
#include <vector>

namespace platform {

// Satu entry route pada jump list. Title adalah label yang dilihat pengguna;
// route_id dipetakan menjadi switch --route oleh implementasi.
struct JumpListRoute {
    std::wstring title;
    std::wstring route_id;
};

// Mengikat identitas taskbar proses agar pin dan jump list selalu terasosiasi.
void ApplyAppUserModelId();

// Memasang task jump list dari daftar route yang diberikan pemanggil. Hanya
// route yang benar-benar ada di resolved UI document yang muncul — tidak ada
// task lain. Modul ini sengaja tidak mengenal config UI, hanya mekanika COM-nya.
void InstallJumpList(std::span<const JumpListRoute> routes);

}  // namespace platform
