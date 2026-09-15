# Changelog

All notable changes to this project are documented here, generated with
[git-cliff](https://git-cliff.org) from [Conventional Commits](https://www.conventionalcommits.org)
commit messages. Commits made before this file existed are grouped
best-effort under "Other".
<!-- git-cliff: end of header -->
## [0.1.16] - 2026-09-15

### 🐛 Bug Fixes

- *(installer)* Preserve user settings when upgrading

## [0.1.15] - 2026-09-15

### 📚 Documentation

- Add CHANGELOG.md and document the Conventional Commits convention

### ⚙️ Miscellaneous Tasks

- Generate release notes with git-cliff instead of --generate-notes

### 💼 Other

- Set HTTP_PROXY/HTTPS_PROXY/ALL_PROXY when enabling the Windows system proxy
- Show build time in the About dialog
- Add zh_CN translation and a Language menu
- Default the close-window prompt to Exit
- Add a Tools menu action to set WSL networking to mirrored mode

## [unreleased]

### 💼 Other

- Set HTTP_PROXY/HTTPS_PROXY/ALL_PROXY when enabling the Windows system proxy
- Show build time in the About dialog
- Add zh_CN translation and a Language menu
- Default the close-window prompt to Exit
- Add a Tools menu action to set WSL networking to mirrored mode
## [0.1.14] - 2026-09-09

### 💼 Other

- Release v0.1.14
## [0.1.13] - 2026-09-09

### 💼 Other

- Release v0.1.13
## [0.1.12] - 2026-09-09

### ⚙️ Miscellaneous Tasks

- Publish Windows release even when macOS signing fails
- Set GH_REPO for gh release commands

### 💼 Other

- Release v0.1.12
## [0.1.11] - 2026-09-09

### 💼 Other

- Release v0.1.11
## [0.1.10] - 2026-09-05

### ⚙️ Miscellaneous Tasks

- Use ws2tcp-local-ffi 0.1.4 from crates.io fallback

### 💼 Other

- Release v0.1.10
## [0.1.9] - 2026-08-24

### 💼 Other

- Release v0.1.9
## [0.1.8] - 2026-08-24

### 💼 Other

- Release v0.1.8
## [0.1.7] - 2026-08-18

### 💼 Other

- Add user settings cleanup support
- Bump version to 0.1.7
## [0.1.6] - 2026-08-18

### 🚀 Features

- Show application version in About dialog

### ⚙️ Miscellaneous Tasks

- Bump version to 0.1.6
## [0.1.5] - 2026-08-18

### 🐛 Bug Fixes

- Bundle MSVC runtime in Windows installer

### 🚜 Refactor

- Define default listener on port 3128

### ⚙️ Miscellaneous Tasks

- Bump version to 0.1.5
## [0.1.3] - 2026-08-10

### 🚀 Features

- Add single-instance application support

### ⚙️ Miscellaneous Tasks

- Bump version to 0.1.3

### 💼 Other

- Add macOS proxy support and release packaging
- Fix macOS proxy activation and reorganize TLS setting
- Update default proxy settings
- Expand form fields on macOS
- Improve settings and exit actions
- Add signed macOS privileged proxy helper
- Fix macOS app icon and native styling
## [0.1.2] - 2026-07-14

### 💼 Other

- Release v0.1.2
## [0.1.1-release.3] - 2026-07-13

### 💼 Other

- Target repository when publishing releases
## [0.1.1-release.2] - 2026-07-13

### 💼 Other

- Publish only the Windows installer
## [0.1.1-release.1] - 2026-07-13

### 💼 Other

- Upgrade Windows workflow to Qt 6.10.3
- Publish installers with GitHub Releases
## [0.1.1] - 2026-07-13

### 💼 Other

- Initial commit
- Enhance CMake configuration for ws2tcp-local-ffi integration
- Add application icons in PNG and SVG formats, and update resource file
- Update .gitignore to include cache directory and fix icon resource path in app.rc
- Implement Windows system proxy management and update MainWindow for proxy settings
- Enable system proxy support for Windows in MainWindow and CMake configuration
- Refactor FFI library handling to use static library and update README instructions
- Fix registry value type serialization
- Update proxy mode dynamically from UI
- Release 0.1.1
- Refactor authentication fields in MainWindow: replace basic auth with separate username and password fields, and add visibility toggle for password
- Add custom rules file selection and close behavior options in MainWindow
- Add Windows installer packaging and About dialog
- Refactor MainWindow: replace buttons with actions for proxy control and add settings dialog
- Enhance CMake configuration to include ws2tcp-local CLI and update README for build instructions
- Disable static configuration while running
- Add Windows x64 release workflow
