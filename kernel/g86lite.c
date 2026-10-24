// SPDX-License-Identifier: GPL-2.0
/*
 * G86Lite v3 - standalone safe display driver for NVIDIA G86 / 8400M GS
 *
 * This driver deliberately DOES NOT initialise PGRAPH, PFIFO, PDISPLAY,
 * clocks, power management, or run the VBIOS.  It adopts a framebuffer
 * which firmware/GRUB has already configured and exposes it through DRM/KMS.
 *
 * The scanout stays exactly as firmware left it.  Xorg/DRM renders to a
 * shmem shadow framebuffer; g86lite copies damaged rectangles into the
 * existing VRAM scanout aperture.
 *
 * Supported PCI ID for now: 10de:0427 (GeForce 8400M GS / G86)
 */

#include <linux/aperture.h>
#include <linux/dma-direction.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/screen_info.h>

#if __has_include(<drm/clients/drm_client_setup.h>)
#include <drm/clients/drm_client_setup.h>
#else
#include <drm/drm_client_setup.h>
#endif
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define G86_VENDOR_ID 0x10de
#define G86_DEVICE_ID 0x0427

static unsigned long long bootfb_addr;
module_param(bootfb_addr, ullong, 0444);
MODULE_PARM_DESC(bootfb_addr, "Physical address of firmware/GRUB framebuffer");

static unsigned int fb_width;
module_param(fb_width, uint, 0444);
MODULE_PARM_DESC(fb_width, "Firmware framebuffer width in pixels");

static unsigned int fb_height;
module_param(fb_height, uint, 0444);
MODULE_PARM_DESC(fb_height, "Firmware framebuffer height in pixels");

static unsigned int fb_stride;
module_param(fb_stride, uint, 0444);
MODULE_PARM_DESC(fb_stride, "Firmware framebuffer stride in bytes");

static unsigned int fb_bpp = 32;
module_param(fb_bpp, uint, 0444);
MODULE_PARM_DESC(fb_bpp, "Firmware framebuffer bits per pixel (only 32 supported)");

struct g86lite_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	struct pci_dev *pdev;

	void __iomem *scanout;
	resource_size_t scanout_phys;
	size_t scanout_size;

	u32 width;
	u32 height;
	u32 stride;
	u32 bpp;
};

static inline struct g86lite_device *to_g86lite(struct drm_device *drm)
{
	return container_of(drm, struct g86lite_device, drm);
}

static inline struct g86lite_device *connector_to_g86lite(struct drm_connector *connector)
{
	return container_of(connector, struct g86lite_device, connector);
}

static int g86lite_read_bootfb(struct g86lite_device *g86)
{
	u64 addr = bootfb_addr;
	u32 width = fb_width;
	u32 height = fb_height;
	u32 stride = fb_stride;
	u32 bpp = fb_bpp;

	/* No module arguments needed on normal x86 VBE/EFI boots. */
	if (!addr)
		addr = __screen_info_lfb_base(&screen_info);
	if (!width)
		width = screen_info.lfb_width;
	if (!height)
		height = screen_info.lfb_height;
	if (!stride)
		stride = screen_info.lfb_linelength;
	if (!bootfb_addr && !fb_width && !fb_height && !fb_stride)
		bpp = screen_info.lfb_depth;

	if (!addr || !width || !height || !stride || !bpp)
		return -ENODEV;

	/* XRGB8888 memory layout on little-endian x86: B,G,R,X. */
	if (!bootfb_addr && screen_info.lfb_depth == 32 &&
	    (screen_info.red_size != 8 || screen_info.red_pos != 16 ||
	     screen_info.green_size != 8 || screen_info.green_pos != 8 ||
	     screen_info.blue_size != 8 || screen_info.blue_pos != 0)) {
		dev_err(&g86->pdev->dev,
			"unsupported firmware pixel layout: R%u@%u G%u@%u B%u@%u\n",
			screen_info.red_size, screen_info.red_pos,
			screen_info.green_size, screen_info.green_pos,
			screen_info.blue_size, screen_info.blue_pos);
		return -EINVAL;
	}

	g86->scanout_phys = (resource_size_t)addr;
	g86->width = width;
	g86->height = height;
	g86->stride = stride;
	g86->bpp = bpp;
	return 0;
}


static enum drm_connector_status
g86lite_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static int g86lite_connector_get_modes(struct drm_connector *connector)
{
	struct g86lite_device *g86 = connector_to_g86lite(connector);
	struct drm_display_mode *mode;

	/*
	 * Timing values are descriptive only. Hardware timing remains whatever
	 * firmware/GRUB programmed before this module was loaded.
	 */
	mode = drm_cvt_mode(&g86->drm, g86->width, g86->height, 60,
			    false, false, false);
	if (!mode)
		return 0;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_connector_helper_funcs g86lite_connector_helper_funcs = {
	.get_modes = g86lite_connector_get_modes,
};

static const struct drm_connector_funcs g86lite_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.detect = g86lite_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static enum drm_mode_status
g86lite_mode_valid(struct drm_simple_display_pipe *pipe,
		   const struct drm_display_mode *mode)
{
	struct g86lite_device *g86 = to_g86lite(pipe->crtc.dev);

	if (mode->hdisplay != g86->width || mode->vdisplay != g86->height)
		return MODE_BAD;

	return MODE_OK;
}

static int g86lite_pipe_check(struct drm_simple_display_pipe *pipe,
			      struct drm_plane_state *plane_state,
			      struct drm_crtc_state *crtc_state)
{
	/* There is intentionally no IRQ/vblank programming in safe mode. */
	crtc_state->no_vblank = true;
	return 0;
}

static void g86lite_copy_rect(struct g86lite_device *g86,
			      struct drm_plane_state *plane_state,
			      const struct drm_rect *rect)
{
	struct drm_shadow_plane_state *shadow;
	struct drm_framebuffer *fb = plane_state->fb;
	const u8 *src;
	u8 __iomem *dst;
	unsigned int x1, x2, y1, y2, y;
	size_t bytes;

	if (!fb)
		return;

	shadow = to_drm_shadow_plane_state(plane_state);
	if (!shadow->data[0].vaddr)
		return;

	x1 = clamp_t(unsigned int, rect->x1, 0, g86->width);
	x2 = clamp_t(unsigned int, rect->x2, x1, g86->width);
	y1 = clamp_t(unsigned int, rect->y1, 0, g86->height);
	y2 = clamp_t(unsigned int, rect->y2, y1, g86->height);

	if (x1 == x2 || y1 == y2)
		return;

	bytes = (x2 - x1) * 4;
	src = shadow->data[0].vaddr;
	dst = (u8 __iomem *)g86->scanout;

	for (y = y1; y < y2; y++) {
		const u8 *s = src + (size_t)y * fb->pitches[0] + (size_t)x1 * 4;
		u8 __iomem *d = dst + (size_t)y * g86->stride + (size_t)x1 * 4;
		memcpy_toio(d, s, bytes);
	}

	/* Flush posted writes before userspace can recycle the buffer. */
	wmb();
}

static void g86lite_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct g86lite_device *g86 = to_g86lite(pipe->crtc.dev);
	struct drm_rect rect = DRM_RECT_INIT(0, 0, g86->width, g86->height);

	g86lite_copy_rect(g86, plane_state, &rect);
}

static void g86lite_pipe_update(struct drm_simple_display_pipe *pipe,
				struct drm_plane_state *old_plane_state)
{
	struct g86lite_device *g86 = to_g86lite(pipe->crtc.dev);
	struct drm_plane_state *new_state = pipe->plane.state;
	struct drm_rect rect;

	if (!new_state || !new_state->fb)
		return;

	if (!drm_atomic_helper_damage_merged(old_plane_state, new_state, &rect))
		return;

	g86lite_copy_rect(g86, new_state, &rect);
}

static const struct drm_simple_display_pipe_funcs g86lite_pipe_funcs = {
	.mode_valid = g86lite_mode_valid,
	.check = g86lite_pipe_check,
	.enable = g86lite_pipe_enable,
	.update = g86lite_pipe_update,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
};

static const u32 g86lite_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u64 g86lite_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

/* drm_mode_config_funcs::fb_create gained a pre-decoded format argument in
 * Linux 6.17. Keep one source tree buildable on both older rescue kernels and
 * the Arch linux-lts 6.18 series.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 17, 0)
static struct drm_framebuffer *
g86lite_fb_create(struct drm_device *dev, struct drm_file *file,
		  const struct drm_format_info *info,
		  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_dirty(dev, file, info, mode_cmd);
}
#else
static struct drm_framebuffer *
g86lite_fb_create(struct drm_device *dev, struct drm_file *file,
		  const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_dirty(dev, file, mode_cmd);
}
#endif

static const struct drm_mode_config_funcs g86lite_mode_config_funcs = {
	.fb_create = g86lite_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(g86lite_fops);

static const struct drm_driver g86lite_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &g86lite_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	.name = "g86lite",
	.desc = "Standalone safe G86 boot-framebuffer DRM driver",
	.major = 0,
	.minor = 3,
};

static int g86lite_validate_bootfb(struct g86lite_device *g86)
{
	resource_size_t bar1_start = pci_resource_start(g86->pdev, 1);
	resource_size_t bar1_len = pci_resource_len(g86->pdev, 1);
	resource_size_t bar1_end;
	resource_size_t fb_end;

	if (!g86->scanout_phys || !g86->width || !g86->height || !g86->stride)
		return -EINVAL;

	if (g86->bpp != 32)
		return -EINVAL;

	if (g86->stride < g86->width * 4)
		return -EINVAL;

	if (!bar1_start || !bar1_len)
		return -ENODEV;

	if (check_add_overflow(bar1_start, bar1_len - 1, &bar1_end))
		return -EOVERFLOW;

	g86->scanout_size = (size_t)g86->stride * g86->height;
	if (!g86->scanout_size)
		return -EINVAL;

	if (check_add_overflow(g86->scanout_phys,
			       (resource_size_t)g86->scanout_size - 1, &fb_end))
		return -EOVERFLOW;

	if (g86->scanout_phys < bar1_start || fb_end > bar1_end) {
		dev_err(&g86->pdev->dev,
			"BOOTFB %pa-%pa is outside BAR1 %pa-%pa\n",
			&g86->scanout_phys, &fb_end,
			&bar1_start, &bar1_end);
		return -ERANGE;
	}

	return 0;
}

static int g86lite_modeset_init(struct g86lite_device *g86)
{
	struct drm_device *drm = &g86->drm;
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.funcs = &g86lite_mode_config_funcs;
	drm->mode_config.min_width = g86->width;
	drm->mode_config.max_width = g86->width;
	drm->mode_config.min_height = g86->height;
	drm->mode_config.max_height = g86->height;
	drm->mode_config.prefer_shadow = true;
	/* XRGB8888 shadow BOs; firmware owns actual scanout format/timing. */
	if (g86->width >= 1920 && g86->height >= 1080)
		drm->mode_config.preferred_depth = 24;

	ret = drm_connector_init(drm, &g86->connector,
				 &g86lite_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	drm_connector_helper_add(&g86->connector,
				 &g86lite_connector_helper_funcs);

	ret = drm_simple_display_pipe_init(drm, &g86->pipe,
					   &g86lite_pipe_funcs,
					   g86lite_formats,
					   ARRAY_SIZE(g86lite_formats),
					   g86lite_modifiers,
					   &g86->connector);
	if (ret)
		return ret;

	/* Allow DIRTYFB/atomic damage to drive shadow-to-VRAM uploads. */
	drm_plane_enable_fb_damage_clips(&g86->pipe.plane);

	drm_mode_config_reset(drm);
	return 0;
}

static int g86lite_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct g86lite_device *g86;
	struct drm_device *drm;
	int ret;

	g86 = devm_drm_dev_alloc(&pdev->dev, &g86lite_drm_driver,
				 struct g86lite_device, drm);
	if (IS_ERR(g86))
		return PTR_ERR(g86);

	drm = &g86->drm;
	g86->pdev = pdev;
	pci_set_drvdata(pdev, drm);

	/* Capture the firmware mode while simpledrm/vesafb still owns it. */
	ret = g86lite_read_bootfb(g86);
	if (ret) {
		dev_err(&pdev->dev, "no usable 32-bpp VBE/EFI framebuffer to adopt\n");
		return ret;
	}

	ret = g86lite_validate_bootfb(g86);
	if (ret) {
		dev_err(&pdev->dev,
			"invalid boot framebuffer: addr=%pa %ux%u stride=%u bpp=%u\n",
			&g86->scanout_phys, g86->width, g86->height,
			g86->stride, g86->bpp);
		return ret;
	}

	ret = pci_enable_device_mem(pdev);
	if (ret)
		return ret;

	/*
	 * Map only the framebuffer that firmware is already scanning out.  We do
	 * not request or touch BAR0.  Keeping the generic framebuffer attached
	 * until our complete DRM device is registered gives us a safer hand-off.
	 */
	g86->scanout = ioremap_wc(g86->scanout_phys, g86->scanout_size);
	if (!g86->scanout) {
		ret = -ENOMEM;
		goto err_disable;
	}

	ret = g86lite_modeset_init(g86);
	if (ret)
		goto err_unmap;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_unmap;

	/*
	 * Now that our DRM node exists, detach simpledrm/vesafb and prevent sysfb
	 * from re-registering it.  This helper also handles legacy VGA ownership.
	 */
	ret = aperture_remove_conflicting_pci_devices(pdev, "g86lite");
	if (ret) {
		drm_dev_unplug(drm);
		goto err_unmap;
	}

	drm_client_setup_with_fourcc(drm, DRM_FORMAT_XRGB8888);

	dev_info(&pdev->dev,
		 "standalone active: %ux%u stride=%u BOOTFB=%pa; BAR0/PGRAPH/PFIFO/PDISPLAY untouched\n",
		 g86->width, g86->height, g86->stride, &g86->scanout_phys);
	return 0;

err_unmap:
	iounmap(g86->scanout);
	g86->scanout = NULL;
err_disable:
	pci_disable_device(pdev);
	return ret;
}

static void g86lite_remove(struct pci_dev *pdev)
{
	struct drm_device *drm = pci_get_drvdata(pdev);
	struct g86lite_device *g86;

	if (!drm)
		return;

	g86 = to_g86lite(drm);
	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	if (g86->scanout) {
		iounmap(g86->scanout);
		g86->scanout = NULL;
	}

	pci_disable_device(pdev);
}

static void g86lite_shutdown(struct pci_dev *pdev)
{
	struct drm_device *drm = pci_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

static const struct pci_device_id g86lite_pci_ids[] = {
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0420) }, /* GeForce 8400 */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0421) }, /* GeForce 8500 GT */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0422) }, /* GeForce 8400 GS */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0423) }, /* GeForce 8300 GS */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0424) }, /* GeForce 8400 GS */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0425) }, /* GeForce 8600M GS */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0426) }, /* GeForce 8400M GT */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0427) }, /* GeForce 8400M GS */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0428) }, /* GeForce 8400M G */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x0429) }, /* Quadro NVS 140M */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042a) }, /* Quadro NVS 130M */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042b) }, /* Quadro NVS 135M */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042c) }, /* GeForce 9400 GT (G86) */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042d) }, /* GeForce 9300M G */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042e) }, /* Quadro FX 360M */
	{ PCI_DEVICE(G86_VENDOR_ID, 0x042f) }, /* GeForce 8400 SE */
	{ }
};
MODULE_DEVICE_TABLE(pci, g86lite_pci_ids);

static struct pci_driver g86lite_pci_driver = {
	.name = "g86lite",
	.id_table = g86lite_pci_ids,
	.probe = g86lite_probe,
	.remove = g86lite_remove,
	.shutdown = g86lite_shutdown,
};

module_pci_driver(g86lite_pci_driver);

MODULE_AUTHOR("G86Lite project");
MODULE_DESCRIPTION("Standalone safe DRM/KMS boot-framebuffer driver for NVIDIA G86 family");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.3.1");
