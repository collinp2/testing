# NECRONAM — building installers

Self-contained installers that drop the plugin into the system plug-in folders
so it shows up in every host. Build the plugin in **Release** first, then run
the installer script for your platform. Output lands in `NECRONAM/dist/`.

```bash
# from the NECRONAM project dir, on both platforms:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Installers are platform-native — a macOS `.pkg` needs Apple's `pkgbuild`, a
Windows `.exe` needs Inno Setup — so each is built on its own OS.

## macOS — `.pkg`

```bash
packaging/macos/build_installer.sh
# → dist/NECRONAM-1.0.0-macOS.pkg
```

Installs:

| Component | Destination |
|-----------|-------------|
| VST3      | `/Library/Audio/Plug-Ins/VST3` |
| AU        | `/Library/Audio/Plug-Ins/Components` |

Options (env vars):

| Var | Effect |
|-----|--------|
| `VERSION=1.2.3` | installer version string (default `1.0.0`) |
| `INCLUDE_STANDALONE=1` | also install the Standalone `.app` to `/Applications` |
| `INSTALLER_SIGN_ID="Developer ID Installer: Name (TEAMID)"` | sign the `.pkg` |

The script only needs the Xcode command-line tools (`pkgbuild` ships with them).
The build is a **universal binary** (arm64 + x86_64), so the installer works on
both Apple Silicon and Intel.

**Distribution:** an unsigned `.pkg` triggers Gatekeeper on other people's Macs.
For public distribution, sign it (`INSTALLER_SIGN_ID=...`) and notarize:

```bash
xcrun notarytool submit dist/NECRONAM-1.0.0-macOS.pkg \
    --apple-id you@example.com --team-id TEAMID --password APP_SPECIFIC_PW --wait
xcrun stapler staple dist/NECRONAM-1.0.0-macOS.pkg
```

## Windows — `.exe`

Requires [Inno Setup 6](https://jrsoftware.org/isinfo.php).

```bat
cd packaging\windows
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" necronam.iss
:: → dist\NECRONAM-1.0.0-Windows.exe
```

Installs:

| Component | Destination |
|-----------|-------------|
| VST3      | `C:\Program Files\Common Files\VST3\NECRONAM.vst3` |
| Standalone (optional) | `C:\Program Files\CP Software\NECRONAM\` + Start-menu shortcut |

Override the version with `/DMyAppVersion=1.2.3`. The installer requests admin
elevation (writing to `Common Files`). For public distribution, sign the
resulting `.exe` with `signtool` and a code-signing certificate.

> Windows has no AU format — only the VST3 (and optional Standalone) are
> packaged there.

## Output

`dist/` is git-ignored — installers are build artefacts, not source.
