# ⚡ g86lite
> **A minimal, fallback DRM/KMS display driver for the NVIDIA G86 GPU family on Linux.**

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL%202.0-blue.svg)](LICENSE)
[![Target Kernel](https://img.shields.io/badge/Kernel-Linux%20LTS%20(6.12.x%2F6.18.x)-orange.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-Generic%20Linux%20(Debian%20%2F%20Fedora%20%2F%20Arch)-brightgreen.svg)](https://kernel.org)

---

## ⚠️ Beta Software & Disclaimer

* **BETA / EXPERIMENTAL RELEASE:** This project is currently in active beta development. While designed to bypass standard lockups, hardware stability varies heavily across degraded silicon. **System freezes, kernel panics, or graphical artifacts can still happen depending on the physical condition of your chip.**
* **NO WARRANTY / DISCLAIMER:** This software is distributed under the GNU General Public License v2.0 in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. Use it entirely at your own risk. The author assumes no responsibility for data loss, system hangs, or hardware damage.

---

## 📌 What is g86lite?

`g86lite` is a standalone, lightweight DRM/KMS display driver created for computers running NVIDIA G86 graphics chips (GeForce 8400M, 8600M, Quadro NVS/FX series). It allows systems with degraded GPUs to boot directly into an X11 desktop environment without triggering the instant hard crashes caused by full drivers (`nouveau` or legacy proprietary blobs).

### 🚫 What is Disabled / Not Working:
* **No 3D Acceleration:** The entire PGRAPH engine is completely uninitialized. OpenGL runs strictly in software mode via LLVMpipe/Mesa (`LIBGL_ALWAYS_SOFTWARE=1`).
* **No Hardware Video Decoding:** PureVideo VP2 / NVDEC engines are turned off. Video playback is handled entirely via CPU software decoding.
* **No Dynamic Clocks / Fan Control:** GPU PLLs, voltage scaling, and power states are not touched.
* **No Multi-Monitor / Dynamic Resolution Switching:** The driver locks to the native scanout resolution preserved by the bootloader (GRUB/EFI/VBE).

---

## 🎯 Supported GPUs (Full G86 Architecture)

Automatically binds to PCI IDs `10de:0420` through `10de:042f`:

| Model | PCI Device ID | Form Factor | Notes |
| :--- | :--- | :--- | :--- |
| **GeForce 8400** | `10de:0420` | Desktop | G86 base revision |
| **GeForce 8500 GT** | `10de:0421` | Desktop | Full G86 desktop card |
| **GeForce 8400 GS** | `10de:0422` | Desktop | G86 desktop variant |
| **GeForce 8300 GS** | `10de:0423` | Desktop OEM | Pre-built OEM systems |
| **GeForce 8400 GS** | `10de:0424` | Desktop | Alternative revision |
| **GeForce 8600M GS** | `10de:0425` | Mobile | 16 shader cores |
| **GeForce 8400M GT** | `10de:0426` | Mobile | Mid-range laptop GPU |
| **GeForce 8400M GS** | `10de:0427` | Mobile | Dell XPS M1330, HP Pavilion, etc. |
| **GeForce 8400M G** | `10de:0428` | Mobile | Entry-level laptop GPU |
| **Quadro NVS 140M** | `10de:0429` | Mobile WS | ThinkPad T61 / R61 |
| **Quadro NVS 130M** | `10de:042a` | Mobile WS | Business notebooks |
| **Quadro NVS 135M** | `10de:042b` | Mobile WS | Dell Latitude D630 / D830 |
| **GeForce 9400 GT** | `10de:042c` | Desktop | G86-D revision |
| **GeForce 9300M G** | `10de:042d` | Mobile | Late revision mobile |
| **Quadro FX 360M** | `10de:042e` | Mobile WS | Precision M2300 |
| **GeForce 8400 SE** | `10de:042f` | Desktop OEM | Low-cost variant |

---

## 🚀 Installation Guide

> **Note:** Developed and primary-tested on **Arch Linux** using the `linux-lts` branch, but designed as standard DKMS source compatible with any modern distribution.

### Prerequisites (Kernel & Headers)
* **Kernel:** **Linux LTS releases (6.12.x / 6.18.x series recommended).** Mainline/rolling kernels frequently break internal DRM client APIs.
* **Headers:** Matching development headers for your installed kernel (`linux-headers-$(uname -r)` or `kernel-devel`).

---

### Option 1: Generic Linux (Debian, Ubuntu, Fedora, openSUSE)

#### 1. Install Build Dependencies & DKMS

* **Debian / Ubuntu / Linux Mint:**
```bash
sudo apt update
sudo apt install build-essential dkms linux-headers-$(uname -r) pciutils xserver-xorg-video-all
```

* **Fedora / RHEL:**
```bash
sudo dnf install gcc make dkms kernel-devel-$(uname -r) pciutils xorg-x11-server-Xorg
```

* **openSUSE:**
```bash
sudo zypper install gcc make dkms kernel-default-devel pciutils
```

#### 2. Register & Build via DKMS
```bash
sudo cp -r kernel /usr/src/g86lite-3.0.1-beta.1
sudo dkms add -m g86lite -v 3.0.1-beta.1
sudo dkms build -m g86lite -v 3.0.1-beta.1
sudo dkms install -m g86lite -v 3.0.1-beta.1
```

#### 3. Blacklist Nouveau & Enable Auto-Load
```bash
sudo cp etc/modprobe.d/g86lite-blacklist-nouveau.conf /etc/modprobe.d/
echo "g86lite" | sudo tee /etc/modules-load.d/g86lite.conf
```

#### 4. Configure GRUB Bootloader
Open `/etc/default/grub` in an editor:
```bash
sudo nano /etc/default/grub
```

Ensure the following parameters are present:
```text
GRUB_CMDLINE_LINUX="module_blacklist=nouveau"
GRUB_GFXMODE="1920x1080x32,1280x800x32,auto"
GRUB_GFXPAYLOAD_LINUX=keep
```

Update GRUB and regenerate your initial ramdisk:

* **Debian / Ubuntu:**
```bash
sudo update-grub
sudo update-initramfs -u
```

* **Fedora / RHEL:**
```bash
sudo grub2-mkconfig -o /boot/grub2/grub.cfg
sudo dracut -f
```

#### 5. Install Xorg Configuration & Utilities
```bash
sudo cp etc/X11/xorg.conf.d/20-g86lite.conf /etc/X11/xorg.conf.d/
sudo install -m 755 tools/g86lite-status /usr/local/bin/g86lite-status
sudo install -m 755 tools/g86lite-verify /usr/local/sbin/g86lite-verify
```

---

### Option 2: Automated Install (Arch Linux)

From the root of the extracted package directory:
```bash
chmod +x tools/*
sudo ./tools/g86lite-install-arch
```

*(See `arch/ARCH-XFCE-SETUP.md` for lightdm and XFCE setup).*

---

### Verification After Reboot

Reboot into the newly configured system:
```bash
sudo reboot
```

After reaching the desktop, run the status utility:
```bash
g86lite-status
```

---

## 📁 Package Contents

```text
g86lite-v3/
├── arch/
│   └── ARCH-XFCE-SETUP.md          # Reference setup guide for Arch Linux + XFCE
├── docs/
│   └── VP2-STATUS.md               # PureVideo HD 2 / hardware decode details
├── etc/
│   ├── modprobe.d/
│   │   └── g86lite-blacklist-nouveau.conf   # Nouveau blacklist configuration
│   ├── systemd/system/
│   │   ├── g86lite-verify.service           # Boot verification service
│   │   └── lightdm.service.d/
│   │       └── g86lite.conf                 # LightDM service override
│   └── X11/xorg.conf.d/
│       └── 20-g86lite.conf                  # Modesetting DDX configuration
├── kernel/
│   ├── dkms.conf                   # DKMS configuration
│   ├── g86lite.c                   # Driver C source code
│   └── Makefile                    # Module Makefile
└── tools/
    ├── g86lite-install-arch        # Arch Linux automated installer
    ├── g86lite-status              # Status and diagnostic script
    └── g86lite-verify              # Boot validation script
```

---

## ⚙️ Architecture & Technical Details

1. **Zero MMIO BAR0 Access:** The driver never accesses MMIO BAR0 space, bypassing VBIOS execution, PGRAPH (3D engine), PFIFO pipelines, and dynamic clocks.
2. **Aperture Hand-off:** Reads active linear scanout attributes from Linux `screen_info` initialized by GRUB and verifies that the framebuffer is positioned safely within VRAM (BAR1).
3. **Shadow Buffer Mechanism:** Desktop compositing and Xorg write into CPU system memory (`shmem`). The kernel module copies damaged rectangles directly into the scanout buffer using write-combining.

---

g86lite | LuxuryApps | 2026
