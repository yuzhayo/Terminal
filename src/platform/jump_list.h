#pragma once

namespace platform {

// Mengikat identitas taskbar proses agar pin dan jump list selalu terasosiasi.
void ApplyAppUserModelId();

// Memasang task jump list (route + exit). Dipanggil setelah window pertama dibuat.
void InstallJumpList();

}  // namespace platform
