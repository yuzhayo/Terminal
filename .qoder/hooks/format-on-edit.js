const { execFileSync } = require('child_process');
const fs = require('fs');

let d = '';
process.stdin.on('data', (c) => (d += c));
process.stdin.on('end', () => {
  try {
    const j = JSON.parse(d);
    const f =
      (j.tool_response && j.tool_response.filePath) ||
      (j.tool_input && j.tool_input.file_path);
    if (!f || !fs.existsSync(f)) return;
    if (/\.(h|hpp|cpp|c)$/i.test(f)) {
      try {
        execFileSync('npx', ['-y', 'clang-format', '-i', `"${f}"`], {
          shell: true,
          windowsHide: true,
          stdio: 'ignore',
        });
      } catch {}
    }
    if (
      /\.(h|hpp|cpp|c|rc|manifest|json|md|ps1|cmd|bat|yml|yaml|txt|props|sln|vcxproj|filters|gitattributes|clang-format)$/i.test(
        f
      )
    ) {
      const s = fs.readFileSync(f, 'utf8');
      if (s.includes('\r')) fs.writeFileSync(f, s.replace(/\r\n/g, '\n'));
    }
  } catch {}
});
