# Arch + XFCE + G86Lite v3.0.1

This assumes Arch has already been installed to disk/USB and boots with GRUB.
For the first installed-system boot, keep the firmware framebuffer active and
make sure Nouveau is not loaded.

Recommended packages for the target system:

    pacman -S --needed linux-lts linux-lts-headers linux-firmware \
        base-devel dkms grub networkmanager pciutils \
        xorg-server xorg-xinit xorg-xrandr \
        xfce4 lightdm lightdm-gtk-greeter mesa

Copy the `g86lite-v3` directory onto the installed system, then run:

    sudo ./tools/g86lite-install-arch
    sudo reboot

After reboot:

    g86lite-status

Expected PCI driver line:

    Kernel driver in use: g86lite

Nouveau must not appear in `lsmod`.

## Resolution

Safe v3 does not program G86 display clocks or timing registers.  It keeps the
linear framebuffer mode selected by GRUB/VBE.  The installer tries, in order:

    1920x1080x32
    1280x800x32
    1024x768x32
    auto

So an external Full-HD monitor should be connected before power-on if 1080p is
the desired test.  If the old NVIDIA VBIOS does not expose a 1920x1080 VBE
mode, v3 will correctly fall back instead of forcing a risky native modeset.
