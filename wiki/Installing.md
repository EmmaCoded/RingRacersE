# Installing

[Home](Home.md)

Ring Racers E uses the data files of the official game (`bios.pk3`, the `data` folder...). It does not include
them and does not change them. You need **Ring Racers 2.4**.

Downloads are on the [Releases](https://github.com/EmmaCoded/RingRacersE/releases) page.

## Windows

1. Install the official [Ring Racers 2.4](https://www.kartkrew.org/) if you don't have it.
2. Download `RingRacersE-2.4E-win64.zip` and put `RingRacersE.exe` in the Ring Racers folder (next to
   `ringracers.exe`). No original file is replaced.
3. Run `RingRacersE.exe`.

The release is a **single executable**: every library is inside it, including ANGLE (DirectX 11 and Vulkan)
and Mesa (DirectX 12). The first time you use DirectX 11, Vulkan or DirectX 12, the game writes those DLLs to
`%LOCALAPPDATA%\RingRacersE\` (about 9 MB for ANGLE and 80 MB for Mesa) and loads them from there. See
[Graphics APIs](Graphics-APIs.md).

Settings are saved in `ringconfigE.cfg`; profiles and records are shared with the official game
([Configuration file](Configuration-File.md)).

**Uninstall:** delete `RingRacersE.exe`, `ringconfigE.cfg` and the folder `%LOCALAPPDATA%\RingRacersE`.

## Linux

1. Download `RingRacersE-2.4E-linux-x86_64.tar.gz` and extract it.
2. Open a terminal in the extracted `RingRacersE` folder and type:

       sh install.sh

   It installs to `~/Games/RingRacersE` and adds "Ring Racers E" to the menu (Games) and the desktop. No
   password is needed.
3. Run "Ring Racers E" from the menu.

**Game data:** the launcher uses the files of the official Ring Racers Flatpak
([Flathub](https://flathub.org/apps/org.kartkrew.RingRacers)) if it is installed, or `bios.pk3` and the `data`
folder copied next to `RingRacersE.sh`. If neither is found, `install.sh` offers to download the official Flatpak
for you (about 700 MB, installed only for your user with `flatpak install --user`). If Flatpak itself is not
installed, it explains how to get it (`sudo apt install flatpak` on Debian, Ubuntu and Mint).

The package includes the libraries the game needs (`lib/` folder), except the ones that must come from your
system: glibc, the video drivers (OpenGL), X11/Wayland, ALSA/PulseAudio and libstdc++.

| File | What it does |
|---|---|
| `install.sh` | Installs or updates Ring Racers E for your user (menu and desktop icons) |
| `uninstall.sh` | Removes Ring Racers E (your settings and replays are kept) |
| `RingRacersE.sh` | Starts the game (also works without installing) |
| `OpenFolder.sh` | Opens `~/.ringracers`: mods (`addons`), logs and settings. Also in the menu as "Ring Racers E - Folder (mods and logs)" |

Settings, replays, mods and logs are in `~/.ringracers` (a hidden folder: Ctrl+H in the file manager). The
official Flatpak keeps its own settings in `~/.var/app/org.kartkrew.RingRacers/.ringracers`.

**Uninstall:** `sh ~/Games/RingRacersE/uninstall.sh`. It does not remove the official Flatpak; to remove it
too: `flatpak uninstall --user org.kartkrew.RingRacers`.

## Minimum requirements

The game itself needs the same as the official Ring Racers. What Ring Racers E adds:

| | Needs |
|---|---|
| Windows | 64-bit Windows 7 SP1 or newer. On Windows 7 the Universal C Runtime update (KB2999226, part of Windows Update) must be installed. Not tested on Windows 7 64-bit. |
| Windows 32-bit | The 32-bit build is not part of this release yet ([Windows 7 and the 32-bit build](Windows-7-and-32-bit.md)). |
| Linux | x86-64 with glibc 2.31 or newer: Debian 11+, Ubuntu 20.04+, Linux Mint 20+, Fedora 32+ and similar. |
| Disk | About 75 MB for the Windows executable, plus up to 90 MB in `%LOCALAPPDATA%\RingRacersE` after using DirectX 11, Vulkan or DirectX 12. About 35 MB for Linux. Plus the official game. |

Each renderer has its own needs. The Rendering Backend menu only lists the ones your PC can use.

| Renderer | Needs |
|---|---|
| Software | Any PC |
| OpenGL (Legacy GL) | An OpenGL driver (Windows and Linux) |
| Direct3D 9 / 9Ex | A Direct3D 9 GPU |
| DirectX 11 (ANGLE) | A Direct3D 11 GPU (feature level 10_0 or higher) |
| Vulkan (ANGLE) | A GPU and driver with Vulkan support |
| DirectX 12 (Mesa) | Windows 10 or 11 and a Direct3D 12 GPU |

On Linux only Software and OpenGL are available.

For slow PCs, turn on [Low-End Mode](Low-End-Mode.md).

**Tested on:** Windows 10 (AMD Ryzen 7 5700U, integrated Radeon graphics) with every renderer; Linux on Q4OS
(Debian 13) on an old laptop; Windows 7 32-bit on another old laptop (it runs, but slowly).
