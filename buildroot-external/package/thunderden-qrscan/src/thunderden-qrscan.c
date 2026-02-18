#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <libv4l2.h>
#include <zbar.h>

#include "psbt_payload_decoder.h"

#define CAPTURE_WIDTH 640
#define CAPTURE_HEIGHT 480
#define MAX_PSBT_BASE64_LEN ((((1024U * 1024U) + 2U) / 3U) * 4U + 1U)

static volatile sig_atomic_t g_stop = 0;

struct tty_ctx {
    int fd;
    int raw_enabled;
    struct termios saved;
};

struct camera_ctx {
    int fd;
    uint32_t pixfmt;
    int width;
    int height;
    size_t frame_size;
    uint8_t *frame;
    uint8_t *rgb;
    uint8_t *gray;
};

struct fb_ctx {
    int enabled;
    int fd;
    size_t mem_len;
    uint8_t *mem;
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
};

static void statusf(const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "Scanner: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void handle_signal(int signo)
{
    (void)signo;
    g_stop = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static int setup_tty_raw(struct tty_ctx *tty)
{
    struct termios raw;

    memset(tty, 0, sizeof(*tty));
    tty->fd = open("/dev/tty", O_RDONLY | O_NONBLOCK);
    if (tty->fd < 0) {
        return -1;
    }

    if (tcgetattr(tty->fd, &tty->saved) != 0) {
        close(tty->fd);
        tty->fd = -1;
        return -1;
    }

    raw = tty->saved;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(tty->fd, TCSANOW, &raw) != 0) {
        close(tty->fd);
        tty->fd = -1;
        return -1;
    }

    tty->raw_enabled = 1;
    return 0;
}

static void restore_tty(struct tty_ctx *tty)
{
    if (tty->fd >= 0 && tty->raw_enabled) {
        tcsetattr(tty->fd, TCSANOW, &tty->saved);
    }

    if (tty->fd >= 0) {
        close(tty->fd);
    }

    tty->fd = -1;
    tty->raw_enabled = 0;
}

static int key_pressed(struct tty_ctx *tty)
{
    char ch;
    ssize_t n;

    if (tty->fd < 0) {
        return 0;
    }

    n = read(tty->fd, &ch, 1);
    if (n > 0) {
        return 1;
    }

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return 1;
    }

    return 0;
}

static int detect_default_device(char *out, size_t out_len)
{
    int idx;

    for (idx = 0; idx <= 9; idx++) {
        if (snprintf(out, out_len, "/dev/video%d", idx) <= 0) {
            continue;
        }

        if (access(out, R_OK) == 0) {
            return 0;
        }
    }

    return -1;
}

static void camera_close(struct camera_ctx *cam)
{
    if (cam->fd >= 0) {
        v4l2_close(cam->fd);
    }

    free(cam->frame);
    free(cam->rgb);
    free(cam->gray);

    memset(cam, 0, sizeof(*cam));
    cam->fd = -1;
}

static int camera_open(struct camera_ctx *cam, const char *device)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    size_t rgb_size;
    size_t gray_size;

    memset(cam, 0, sizeof(*cam));
    cam->fd = -1;

    cam->fd = v4l2_open(device, O_RDWR | O_NONBLOCK);
    if (cam->fd < 0) {
        statusf("Unable to open %s: %s", device, strerror(errno));
        return -1;
    }

    memset(&cap, 0, sizeof(cap));
    if (v4l2_ioctl(cam->fd, VIDIOC_QUERYCAP, &cap) != 0) {
        statusf("Unable to query camera capabilities: %s", strerror(errno));
        camera_close(cam);
        return -1;
    }

    if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        statusf("Camera does not support video capture");
        camera_close(cam);
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = CAPTURE_WIDTH;
    fmt.fmt.pix.height = CAPTURE_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (v4l2_ioctl(cam->fd, VIDIOC_S_FMT, &fmt) != 0) {
        statusf("Unable to set camera format: %s", strerror(errno));
        camera_close(cam);
        return -1;
    }

    cam->pixfmt = fmt.fmt.pix.pixelformat;
    cam->width = (int)fmt.fmt.pix.width;
    cam->height = (int)fmt.fmt.pix.height;

    if (cam->width <= 0 || cam->height <= 0) {
        statusf("Camera returned invalid frame size");
        camera_close(cam);
        return -1;
    }

    if (cam->pixfmt != V4L2_PIX_FMT_RGB24 && cam->pixfmt != V4L2_PIX_FMT_BGR24 && cam->pixfmt != V4L2_PIX_FMT_YUYV) {
        statusf("Unsupported camera pixel format: 0x%08x", cam->pixfmt);
        camera_close(cam);
        return -1;
    }

    cam->frame_size = fmt.fmt.pix.sizeimage;
    if (cam->pixfmt == V4L2_PIX_FMT_YUYV) {
        size_t min_sz = (size_t)cam->width * (size_t)cam->height * 2U;
        if (cam->frame_size < min_sz) {
            cam->frame_size = min_sz;
        }
    } else {
        size_t min_sz = (size_t)cam->width * (size_t)cam->height * 3U;
        if (cam->frame_size < min_sz) {
            cam->frame_size = min_sz;
        }
    }

    rgb_size = (size_t)cam->width * (size_t)cam->height * 3U;
    gray_size = (size_t)cam->width * (size_t)cam->height;

    cam->frame = malloc(cam->frame_size);
    cam->rgb = malloc(rgb_size);
    cam->gray = malloc(gray_size);

    if (cam->frame == NULL || cam->rgb == NULL || cam->gray == NULL) {
        statusf("Unable to allocate camera frame buffers");
        camera_close(cam);
        return -1;
    }

    statusf("Camera format: %dx%d (pixfmt=0x%08x)", cam->width, cam->height, cam->pixfmt);
    return 0;
}

static ssize_t camera_read_frame(struct camera_ctx *cam)
{
    fd_set rfds;
    struct timeval tv;
    int ret;
    ssize_t n;

    FD_ZERO(&rfds);
    FD_SET(cam->fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 150000;

    ret = select(cam->fd + 1, &rfds, NULL, NULL, &tv);
    if (ret < 0) {
        if (errno == EINTR) {
            return 0;
        }
        statusf("Camera wait failed: %s", strerror(errno));
        return -1;
    }

    if (ret == 0) {
        return 0;
    }

    n = v4l2_read(cam->fd, cam->frame, cam->frame_size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR || errno == EIO) {
            return 0;
        }
        statusf("Camera read failed: %s", strerror(errno));
        return -1;
    }

    return n;
}

static inline uint8_t clip_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static int convert_frame(struct camera_ctx *cam, ssize_t nread)
{
    size_t pixels = (size_t)cam->width * (size_t)cam->height;
    size_t i;

    if (cam->pixfmt == V4L2_PIX_FMT_RGB24) {
        const uint8_t *src = cam->frame;

        if ((size_t)nread < pixels * 3U) {
            return -1;
        }

        memcpy(cam->rgb, src, pixels * 3U);
        for (i = 0; i < pixels; i++) {
            uint8_t r = src[3U * i + 0U];
            uint8_t g = src[3U * i + 1U];
            uint8_t b = src[3U * i + 2U];
            cam->gray[i] = (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
        }
        return 0;
    }

    if (cam->pixfmt == V4L2_PIX_FMT_BGR24) {
        const uint8_t *src = cam->frame;

        if ((size_t)nread < pixels * 3U) {
            return -1;
        }

        for (i = 0; i < pixels; i++) {
            uint8_t b = src[3U * i + 0U];
            uint8_t g = src[3U * i + 1U];
            uint8_t r = src[3U * i + 2U];
            cam->rgb[3U * i + 0U] = r;
            cam->rgb[3U * i + 1U] = g;
            cam->rgb[3U * i + 2U] = b;
            cam->gray[i] = (uint8_t)((77U * r + 150U * g + 29U * b) >> 8);
        }
        return 0;
    }

    if (cam->pixfmt == V4L2_PIX_FMT_YUYV) {
        const uint8_t *src = cam->frame;
        size_t pairs = pixels / 2U;

        if ((size_t)nread < pixels * 2U) {
            return -1;
        }

        for (i = 0; i < pairs; i++) {
            int y0 = src[4U * i + 0U];
            int u = src[4U * i + 1U] - 128;
            int y1 = src[4U * i + 2U];
            int v = src[4U * i + 3U] - 128;
            int c0 = y0 - 16;
            int c1 = y1 - 16;
            int r0 = (298 * c0 + 409 * v + 128) >> 8;
            int g0 = (298 * c0 - 100 * u - 208 * v + 128) >> 8;
            int b0 = (298 * c0 + 516 * u + 128) >> 8;
            int r1 = (298 * c1 + 409 * v + 128) >> 8;
            int g1 = (298 * c1 - 100 * u - 208 * v + 128) >> 8;
            int b1 = (298 * c1 + 516 * u + 128) >> 8;
            size_t p0 = 2U * i;
            size_t p1 = p0 + 1U;

            cam->rgb[3U * p0 + 0U] = clip_u8(r0);
            cam->rgb[3U * p0 + 1U] = clip_u8(g0);
            cam->rgb[3U * p0 + 2U] = clip_u8(b0);
            cam->rgb[3U * p1 + 0U] = clip_u8(r1);
            cam->rgb[3U * p1 + 1U] = clip_u8(g1);
            cam->rgb[3U * p1 + 2U] = clip_u8(b1);

            cam->gray[p0] = (uint8_t)y0;
            cam->gray[p1] = (uint8_t)y1;
        }

        return 0;
    }

    return -1;
}

static void fb_close(struct fb_ctx *fb)
{
    if (fb->mem != NULL && fb->mem_len > 0) {
        munmap(fb->mem, fb->mem_len);
    }
    if (fb->fd >= 0) {
        close(fb->fd);
    }
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;
}

static int fb_open(struct fb_ctx *fb)
{
    memset(fb, 0, sizeof(*fb));
    fb->fd = -1;

    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) {
        statusf("Live preview unavailable (/dev/fb0): %s", strerror(errno));
        return -1;
    }

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fix) != 0 || ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->var) != 0) {
        statusf("Live preview unavailable (fb info): %s", strerror(errno));
        fb_close(fb);
        return -1;
    }

    if (fb->var.bits_per_pixel != 16 && fb->var.bits_per_pixel != 24 && fb->var.bits_per_pixel != 32) {
        statusf("Live preview unavailable (unsupported framebuffer depth: %u)", fb->var.bits_per_pixel);
        fb_close(fb);
        return -1;
    }

    fb->mem_len = fb->fix.smem_len;
    fb->mem = mmap(NULL, fb->mem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) {
        fb->mem = NULL;
        statusf("Live preview unavailable (mmap): %s", strerror(errno));
        fb_close(fb);
        return -1;
    }

    memset(fb->mem, 0, fb->mem_len);
    fb->enabled = 1;
    statusf("Live preview enabled.");
    return 0;
}

static inline uint32_t pack_fb_pixel(const struct fb_ctx *fb, uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t pixel = 0;

    if (fb->var.red.length > 0) {
        pixel |= ((uint32_t)r >> (8U - fb->var.red.length)) << fb->var.red.offset;
    }
    if (fb->var.green.length > 0) {
        pixel |= ((uint32_t)g >> (8U - fb->var.green.length)) << fb->var.green.offset;
    }
    if (fb->var.blue.length > 0) {
        pixel |= ((uint32_t)b >> (8U - fb->var.blue.length)) << fb->var.blue.offset;
    }

    return pixel;
}

static void fb_draw_rgb(const struct fb_ctx *fb, const uint8_t *rgb, int width, int height)
{
    int bytespp;
    int fb_w;
    int fb_h;
    int draw_w;
    int draw_h;
    int x_off;
    int y_off;
    int y;

    if (!fb->enabled) {
        return;
    }

    bytespp = (int)(fb->var.bits_per_pixel / 8U);
    fb_w = (int)fb->var.xres;
    fb_h = (int)fb->var.yres;
    draw_w = (width < fb_w) ? width : fb_w;
    draw_h = (height < fb_h) ? height : fb_h;
    x_off = (fb_w - draw_w) / 2;
    y_off = (fb_h - draw_h) / 2;

    for (y = 0; y < draw_h; y++) {
        const uint8_t *src = rgb + (size_t)y * (size_t)width * 3U;
        uint8_t *dst = fb->mem + (size_t)(y + y_off + (int)fb->var.yoffset) * fb->fix.line_length +
                       (size_t)(x_off + (int)fb->var.xoffset) * (size_t)bytespp;
        int x;

        for (x = 0; x < draw_w; x++) {
            uint8_t r = src[3 * x + 0];
            uint8_t g = src[3 * x + 1];
            uint8_t b = src[3 * x + 2];
            uint32_t p = pack_fb_pixel(fb, r, g, b);

            if (bytespp == 4) {
                dst[4 * x + 0] = (uint8_t)(p & 0xffU);
                dst[4 * x + 1] = (uint8_t)((p >> 8U) & 0xffU);
                dst[4 * x + 2] = (uint8_t)((p >> 16U) & 0xffU);
                dst[4 * x + 3] = (uint8_t)((p >> 24U) & 0xffU);
            } else if (bytespp == 3) {
                dst[3 * x + 0] = (uint8_t)(p & 0xffU);
                dst[3 * x + 1] = (uint8_t)((p >> 8U) & 0xffU);
                dst[3 * x + 2] = (uint8_t)((p >> 16U) & 0xffU);
            } else if (bytespp == 2) {
                dst[2 * x + 0] = (uint8_t)(p & 0xffU);
                dst[2 * x + 1] = (uint8_t)((p >> 8U) & 0xffU);
            }
        }
    }
}

static int scan_frame_for_psbt(zbar_image_scanner_t *scanner,
                               struct psbt_payload_decoder *decoder,
                               const uint8_t *gray,
                               int width,
                               int height,
                               char *out,
                               size_t out_len)
{
    zbar_image_t *img;
    int count;

    img = zbar_image_create();
    if (img == NULL) {
        return 0;
    }

    zbar_image_set_format(img, zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(img, (unsigned)width, (unsigned)height);
    zbar_image_set_data(img, gray, (unsigned long)((size_t)width * (size_t)height), NULL);

    count = zbar_scan_image(scanner, img);
    if (count > 0) {
        const zbar_symbol_t *sym = zbar_image_first_symbol(img);
        while (sym != NULL) {
            const char *data = zbar_symbol_get_data(sym);
            unsigned int data_len = zbar_symbol_get_data_length(sym);

            if (data != NULL && data_len > 0U &&
                psbt_payload_decoder_consume(decoder, (const uint8_t *)data, (size_t)data_len, out, out_len)) {
                zbar_image_destroy(img);
                return 1;
            }
            sym = zbar_symbol_next(sym);
        }
    }

    zbar_image_destroy(img);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--device /dev/videoX]\n"
            "\n"
            "Capture camera frames, render live preview to framebuffer, and\n"
            "print the first normalized base64 PSBT found in supported QR payload formats.\n",
            prog);
}

int main(int argc, char **argv)
{
    struct tty_ctx tty;
    struct camera_ctx cam;
    struct fb_ctx fb;
    zbar_image_scanner_t *scanner = NULL;
    struct psbt_payload_decoder *decoder = NULL;
    char device[64] = {0};
    char *psbt = NULL;
    int i;
    int ret = 1;

    memset(&tty, 0, sizeof(tty));
    tty.fd = -1;
    memset(&cam, 0, sizeof(cam));
    cam.fd = -1;
    memset(&fb, 0, sizeof(fb));
    fb.fd = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--device") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            strncpy(device, argv[++i], sizeof(device) - 1U);
            continue;
        }
        if (device[0] == '\0') {
            strncpy(device, argv[i], sizeof(device) - 1U);
            continue;
        }
        usage(argv[0]);
        return 1;
    }

    if (device[0] == '\0') {
        if (detect_default_device(device, sizeof(device)) != 0) {
            statusf("No camera device found under /dev/video*");
            return 1;
        }
    }

    install_signal_handlers();

    if (setup_tty_raw(&tty) != 0) {
        statusf("Unable to enable any-key cancel mode; Ctrl+C still works.");
    }

    statusf("Opening camera device: %s", device);
    if (camera_open(&cam, device) != 0) {
        goto out;
    }

    if (fb_open(&fb) != 0) {
        statusf("Scanning continues without framebuffer preview.");
    }

    psbt = malloc(MAX_PSBT_BASE64_LEN);
    if (psbt == NULL) {
        statusf("Unable to allocate PSBT output buffer.");
        goto out;
    }

    scanner = zbar_image_scanner_create();
    if (scanner == NULL) {
        statusf("Unable to initialize QR scanner.");
        goto out;
    }

    zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 0);
    zbar_image_scanner_set_config(scanner, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);

    decoder = psbt_payload_decoder_create();
    if (decoder == NULL) {
        statusf("Unable to initialize payload decoder.");
        goto out;
    }

    statusf("Press any key to cancel scanning and return to menu.");
    statusf("Waiting for PSBT QR (base64, UR, BBQR, pMofN, hex, base43)...");

    while (!g_stop) {
        ssize_t nread;

        if (key_pressed(&tty)) {
            statusf("Scan cancelled by user.");
            ret = 130;
            goto out;
        }

        nread = camera_read_frame(&cam);
        if (nread < 0) {
            goto out;
        }
        if (nread == 0) {
            continue;
        }

        if (convert_frame(&cam, nread) != 0) {
            statusf("Camera returned unsupported frame data.");
            goto out;
        }

        if (fb.enabled) {
            fb_draw_rgb(&fb, cam.rgb, cam.width, cam.height);
        }

        if (scan_frame_for_psbt(scanner, decoder, cam.gray, cam.width, cam.height, psbt, MAX_PSBT_BASE64_LEN)) {
            printf("%s\n", psbt);
            fflush(stdout);
            statusf("QR captured. Moving to mnemonic input...");
            ret = 0;
            goto out;
        }
    }

    statusf("Scan cancelled by signal.");
    ret = 130;

out:
    psbt_payload_decoder_destroy(decoder);
    if (scanner != NULL) {
        zbar_image_scanner_destroy(scanner);
    }
    free(psbt);
    fb_close(&fb);
    camera_close(&cam);
    restore_tty(&tty);
    return ret;
}
