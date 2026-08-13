# Agent Guide — Open Terminal

Dokumen ini berlaku untuk `C:\VSCODE\Teminal` dan seluruh subfoldernya. Instruksi user selalu menjadi prioritas utama.

## Source ownership

- `Open-terminal`: source canonical untuk aplikasi WinForms + WebView2 yang sudah ada.
- `Open-terminal-native`: aplikasi standalone C++ Win32 yang terpisah dan aktif untuk development
  sesuai `Open-terminal-native\plan.md`.
- Jika task menargetkan `Open-terminal-native`, seluruh project exploration dan perubahan harus tetap
  di folder tersebut; jangan scan atau memakai `Open-terminal` sebagai referensi maupun baseline.
- `Terminal-v1`: legacy/reference; perlakukan read-only kecuali user meminta perubahan secara eksplisit.
- `docs\PAT.txt`: credential lokal di luar repository. File ini harus tetap berada di PC dan tidak boleh dibaca, ditampilkan, dipindahkan, dihapus, atau di-commit oleh agent.
- Folder `Teminal-Next` dan `Teminal-Next-Fast` sudah dihentikan dan tidak boleh dijadikan dependency atau dibuat ulang tanpa permintaan eksplisit.

## Development environment

1. Buka dan kerjakan hanya folder aplikasi yang disebut user: `Open-terminal` untuk aplikasi lama
   atau `Open-terminal-native` untuk aplikasi native.
2. Gunakan terminal PowerShell terintegrasi VS Code untuk validasi dan perintah GitHub Actions.
3. Gunakan SSH untuk Git push dan browser authentication untuk GitHub CLI.
4. Jangan menyimpan token, password, key, installer, package, DLL, PDB, atau WebView cache di repository.
5. Pertahankan settings aplikasi di `%LOCALAPPDATA%\OpenTerminal`.
6. Pertahankan tiga aksi terminal: PowerShell Admin, PowerShell, dan Ubuntu (WSL).

## Architecture boundaries

- Pertahankan pemisahan terminal launching, settings persistence, WebView UI, Windows integration, dan updater coordination.
- Jangan menjalankan network update check sebelum launcher terlihat.
- Gunakan per-user registry untuk Explorer context-menu registration.
- Gunakan Velopack melalui `tools\Build-Release.ps1`; Inno Setup bukan bagian dari source canonical.
- Pin package Velopack dan local `vpk` tool ke versi yang sama.

## Validation minimum

```powershell
Set-Location C:\VSCODE\Teminal\Open-terminal
dotnet tool restore
dotnet restore .\TerminalChooser.csproj --locked-mode
.\tools\Test-Source.ps1
dotnet build .\TerminalChooser.csproj -c Release --no-restore
```

Untuk perubahan launcher behavior, packaging, atau UI, jalankan build release dan smoke test yang relevan. Perubahan visual membutuhkan bukti Windows; build PASS tidak membuktikan tampilan pixel-perfect.

## VS Code release discipline

- Ikuti `Open-terminal\docs\VSCODE-RELEASE.md` sebagai prosedur canonical.
- Push ke `main` menjalankan CI tetapi tidak otomatis membuat release.
- Jangan trigger workflow Release tanpa permintaan user yang eksplisit.
- Pastikan CI untuk commit terbaru PASS sebelum release.
- Trigger release hanya sekali, lalu pantau run yang sudah dibuat.
- `TerminalChooser.csproj` adalah version source of truth; workflow menyinkronkan versi saat release.
- Jangan overwrite tag atau asset release yang sudah ada.
- Laporkan commit, run URL, tag, asset, dan hasil validasi setelah release.

Perintah release dari terminal VS Code:

```powershell
gh workflow run release.yml --repo yuzhayo/Open-terminal --ref main -f bump=patch
```

Gunakan `minor` atau `major` hanya jika perubahan semver tersebut memang diminta.
