# XM8M

<p align="center">
  <img src="Documents/AppIcon.png" alt="XM8M" width="128" height="128">
</p>

[日本語](README.md)

XM8M is a multi-platform NEC PC-8801 emulator.

<p align="center">
  <a href="https://github.com/bubio/xm8m/releases/latest">
    <img src="https://img.shields.io/github/v/release/bubio/xm8m" alt="Latest Release">
  </a>
  <a href="https://github.com/bubio/xm8m/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/bubio/xm8m" alt="License">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Windows_x86_x64_arm64.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Windows_x86_x64_arm64.yml/badge.svg" alt="Windows">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/macOS_Universal.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/macOS_Universal.yml/badge.svg" alt="macOS">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Linux_x86_64.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Linux_x86_64.yml/badge.svg" alt="Linux x86_64">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Linux_x86_64_AppImage.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Linux_x86_64_AppImage.yml/badge.svg" alt="Linux x86_64 AppImage">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Linux_arm64.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Linux_arm64.yml/badge.svg" alt="Linux arm64">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Linux_arm64_AppImage.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Linux_arm64_AppImage.yml/badge.svg" alt="Linux arm64 AppImage">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/RaspberryPiOS_armhf.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/RaspberryPiOS_armhf.yml/badge.svg" alt="Raspberry Pi OS">
  </a>
  <a href="https://github.com/bubio/xm8m/actions/workflows/Android.yml">
    <img src="https://github.com/bubio/xm8m/actions/workflows/Android.yml/badge.svg" alt="Android">
  </a>
  <a href="https://github.com/bubio/xm8m/releases/latest">
    <img src="https://img.shields.io/github/downloads/bubio/xm8m/total.svg" alt="Downloads">
  </a>
</p>

## About XM8M

XM8M is a multi-platform emulator for the PC-8801MA, which is compatible with the PC-8801mkIISR, based on XM8 by P.I. Its former name was xm8mac.

<p align="center">
  <img width="752" src="Documents/Screenshot.png" alt="XM8 Screenshot">
</p>

This repository was created with permission from P.I.

Development primarily focuses on the unofficial macOS version, while Windows, Linux, and Android builds are also distributed where possible.

The original XM8 website is:

http://retropc.net/pi/xm8/index.html

## Installation

Download the executable for your platform from the [Releases](https://github.com/bubio/xm8m/releases) page.

For macOS, move `XM8.app` to the `Applications` folder or another suitable location, then run it.

### System Requirements

| Platform | CPU | Minimum OS version | Download |
| --- | --- | --- | --- |
| macOS | x86_64 | macOS 10.13 High Sierra | [Universal build](https://github.com/bubio/xm8m/releases/latest/download/XM8_macOS_Universal.dmg) |
| macOS | Apple Silicon | macOS 11 Big Sur | [Universal build](https://github.com/bubio/xm8m/releases/latest/download/XM8_macOS_Universal.dmg) |
| Windows | x86_64 / x86_32 / ARM64 | Windows 10 | [x86_64 build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Windows_x86_64.zip) / [x86_32 build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Windows_x86_32.zip) / [ARM64 build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Windows_ARM64.zip) |
| Linux | x86_64 / arm64 | Ubuntu 22.04 or later (.deb) / Fedora 36 or later (.rpm) | [x86_64 build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Linux_x86_64.deb) / [arm64 build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Linux_arm64.deb) |
| Raspberry Pi OS | armhf | Bookworm or later | [armhf build](https://github.com/bubio/xm8m/releases/latest/download/XM8_Linux_armhf.deb) |
| Android | arm64-v8a / armeabi-v7a / x86_64 | Android 4.4 (API level 19) or later | [APK](https://github.com/bubio/xm8m/releases/latest/download/XM8_Android.apk) |

On Linux, install the required dependency libraries for your distribution with its package manager. RetroAchievements in the Android build requires Android 6.0 (API level 23) or later.

## ROM Files

For information on supported ROM files, see the `[ROM files]` section in [README-XM8.txt](Documents/README-XM8.txt).

### Location

Place ROM files in the following directory, which is also where the settings file is stored. The directory is created after the application is launched once.

```shell
~/Library/Application Support/retro_pc_pi/xm8
```

## Usage

See the `[How to use]` section in [README-XM8.txt](Documents/README-XM8.txt).

### Screenshots

Press **F12** to save the game screen as a PNG. The shortcut is fixed to F12 on every platform; keyboards using F12 for volume may require Fn. **Save Screenshot** in the main menu is available on every platform, including Android touch screens. Use F11 or Esc to leave the menu.

Screenshots are 640×400, with doubled rows for 200-line video. Menus, software keyboards, RA notifications, status indicators and display filters are excluded. While a menu is open, the last game frame before entering the menu is saved.

| OS | Destination |
| --- | --- |
| macOS | `XM8/Screenshots` inside the system Pictures folder, normally `~/Pictures/XM8/Screenshots` |
| Windows | `XM8/Screenshots` inside the Pictures known folder, respecting relocation and OneDrive |
| Linux / Raspberry Pi OS | `XM8/Screenshots` inside the xdg-user-dirs Pictures folder, falling back to `~/Pictures/XM8/Screenshots` |
| Android | Shared `Pictures/XM8/Screenshots`, visible in gallery apps |

On desktop, **Open Screenshot Folder** opens the destination. Files use local timestamps such as `XM8_20260904_213045_123.png`, with numeric suffixes to prevent overwrites. A three-second notice confirms completion. Files are written in the background, and pending writes finish before the app exits.

Android 10 and later need no additional storage permission for screenshot saving. On Android 6–9, allow storage access when prompted, then capture again. If denied, enable storage permission in Android app settings. Android 4.4–5.1 use legacy storage permissions.

### Command-line Launch

On Windows, macOS, and Linux, you can specify up to two D88 images on the command line. They are inserted into drives 1 and 2 in the specified order. Playlists in `.m3u` and `.m3u8` format are supported.

```shell
xm8 [options] [--] [disk-spec ...]

xm8 game.d88
xm8 game.d88#1
xm8 system.d88#0 data.d88#1
xm8 game.m3u
xm8 game.m3u8
xm8 --system V1H --clock 4MHz game.d88
```

`disk-spec` can be `<path>`, `<path>#<bank>`, or `<path>:<bank>`. Banks are zero-indexed.

Options:

```text
--system <V1S|V1H|V2|N>
--clock <4|4MHz|8|8MHz|8H|8MHzH>
-h, --help
--version
```

`--system` and `--clock` apply only to the current launch; they are not saved to the normal configuration or automatic save state. Paths whose final path component contains `#` or `:` cannot be specified on the command line because they are ambiguous with a bank selector.

### RetroAchievements

In builds with RetroAchievements support, use `RetroAchievements` in the main menu to sign in and switch RA modes. Supported games can use achievements, leaderboards, Rich Presence, and Hardcore mode.

Hardcore operation restrictions follow the
[official Hardcore compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html).
Debug save-state creation remains available in Hardcore;
loading states, normal/automatic state creation, and Full Speed remain prohibited.

XM8M is completely free and non-commercial. It has no advertising, in-app purchases, paid edition, or feature-bearing donation rewards.

In RA mode, the selected D88 is registered in the library and an app-managed working copy is used, leaving the original unchanged. A game can still start if authentication or network access fails, but achievements are not evaluated or submitted for that session.

The Android build runs on API level 19 and later, but the RetroAchievements UI, networking, and credential storage are available only on Android 6.0 (API level 23) and later.

[Privacy policy](PRIVACY.md) / [日本語](PRIVACY.ja.md) / [Component licenses](LICENSES.md)

## Building

### Requirements

Install the following before building:

- Xcode

  Only the command-line tools are used, but installing Xcode is the easiest way to obtain them.

- Homebrew

  Install [Homebrew](https://brew.sh/) to obtain build tools such as CMake.

### macOS Build

Open a terminal at the project root and run:

```shell
cd Builder/macOS
./dist_app.sh
```

The application bundle (`.app`) is created in the `build` directory.

## Acknowledgements

Thank you to P.I. for kindly permitting this source-code adaptation.

## Other Operating Systems

### Windows

`Builder/Windows` contains a Visual Studio 2022 solution.

Run `setup_sdl2.ps1` in `Builder/Windows` to download and place the SDL2 files required for building:

- `Builder\\Windows\\SDL\\include` (header files)
- `Builder\\Windows\\SDL\\lib\\x86` (x86 libraries)
- `Builder\\Windows\\SDL\\lib\\x64` (x64 libraries)
- `Builder\\Windows\\SDL\\lib\\arm64` (ARM64 libraries)

Build `Builder/Windows/XM8.sln` with Visual Studio. Output is generated in `Builder/Windows/x64`, `Builder/Windows/Win32`, or `Builder/Windows/ARM64`. `XM8.exe` and `SDL2.dll` are required to run it.

RetroAchievements is enabled in the normal Windows distribution builds. If a compatibility-test build that completely excludes the feature is needed, specify the shared `XM8_ENABLE_RETROACHIEVEMENTS=false` property from Developer PowerShell:

```powershell
msbuild Builder\Windows\XM8.sln /m /p:Configuration=Release /p:Platform=x64 /p:XM8_ENABLE_RETROACHIEVEMENTS=true
msbuild Builder\Windows\XM8.sln /m /p:Configuration=Release /p:Platform=x64 /p:XM8_ENABLE_RETROACHIEVEMENTS=false
```

For RA-enabled builds, `THIRD_PARTY_NOTICES.md` and the `Licenses` directory are also copied to the output directory. The same property is available for Debug and Release builds of Win32, x64, and ARM64.

Place BIOS ROM files in:

```shell
%appdata%\retro_pc_pi\xm8
```

### Linux

`Builder/Linux` contains scripts for creating deb, rpm, and AppImage packages. See `dist_app.sh` for required build libraries.

#### deb or rpm

```shell
cd Builder/Linux
./dist_app.sh
```

This creates a deb or rpm package in the `build` directory.

#### AppImage

```shell
cd Builder/Linux
./appimage.sh
```

This creates an AppImage file in `Builder/Linux`.

#### Flatpak (RetroAchievements enabled)

The same manifest natively builds x86_64 and arm64 Flatpak packages. The Freedesktop 25.08 runtime is required:

```shell
flatpak install flathub org.freedesktop.Sdk//25.08 org.freedesktop.Platform//25.08
flatpak-builder --user --install --force-clean flatpak-build com.github.bubio.xm8m.yml
```

The package grants network and Secret Service access for RetroAchievements, and home-directory access because XM8M uses SDL's file picker for ROM and media files. Place BIOS ROM files in the Flatpak data directory:

```shell
~/.var/app/com.github.bubio.xm8m/data/retro_pc_pi/xm8/
```

Place BIOS ROM files in:

```shell
~/.local/share/retro_pc_pi/xm8/
```

### Android

`Builder/Android` contains an Android Studio project.

Run `setup_sdl2.sh` in `Builder/Android` to download and place the SDL2 files required for building:

- `Builder/Android/app/jni/SDL/include` (header files)
- `Builder/Android/app/jni/SDL/src` (source files)
- `Builder/Android/app/src/java/org/libsdl/app` (Java source files)

Open `Builder/Android` in Android Studio and build the project.

Place BIOS ROM files in:

```shell
Android/data/retro_pc_pi/files/
```

On Android 11 and later, files on the device cannot be accessed freely. It is recommended to place game disk images in the same location.

## Open-source Software Licenses

| Component | Version | License |
| --- | --- | --- |
| [xBRZ](https://sourceforge.net/projects/xbrz/) | bundled | GPLv3 |
| [rcheevos](https://github.com/RetroAchievements/rcheevos) | 12.3.0 | MIT |
| [SQLite](https://www.sqlite.org/) | 3.53.0 | Public Domain |
| [stb_image](https://github.com/nothings/stb) | 2.30 | MIT OR Public Domain |

rcheevos, SQLite, and stb_image are used in builds with RetroAchievements support. For dependency versions, provenance, and full license text, see [ThirdParty/README.md](ThirdParty/README.md), [ThirdParty/THIRD_PARTY_NOTICES.md](ThirdParty/THIRD_PARTY_NOTICES.md), and `ThirdParty/licenses/`.
