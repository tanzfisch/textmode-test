#if 0
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <iostream>
#include <cstdint>
#include <linux/input.h>
#include <chrono>
#include <thread>

void draw_rectangle(Framebuffer& fb, int x, int y, int width, int height, uint32_t color) {
    for (int j = y; j < y + height && j < (int)fb.vinfo.yres; ++j) {
        if (j < 0) continue;
        for (int i = x; i < x + width && i < (int)fb.vinfo.xres; ++i) {
            if (i < 0) continue;
            int offset = j * (fb.finfo.line_length / sizeof(uint32_t)) + i;
            fb.buffer[offset] = color;
        }
    }
}

void clear_framebuffer(Framebuffer& fb, uint32_t color) {
    for (size_t i = 0; i < fb.size / sizeof(uint32_t); ++i) {
        fb.buffer[i] = color;
    }
}

int main() {
    Framebuffer fb = {};
    if (!init_framebuffer(fb)) {
        return 1;
    }

    const char mouseDev[] = "/dev/input/mouse0";

    int mouse_fd = open(mouseDev, O_RDONLY | O_NONBLOCK);
    if (mouse_fd == -1) {
        std::cerr << "Error opening " << mouseDev << std::endl;
        munmap(fb.buffer, fb.size);
        close(fb.fd);
        return 1;
    }

    // Assuming 32-bit color (RGBA)
    uint32_t green = 0x00FF00FF; // Green rectangle
    uint32_t black = 0x000000FF; // Black background
    int rect_width = 50;
    int rect_height = 50;
    int mouse_x = fb.vinfo.xres / 2; // Start at screen center
    int mouse_y = fb.vinfo.yres / 2;

    struct input_event ev;
    bool running = true;

    while (running) {
        // Read mouse events (non-blocking)
        while (read(mouse_fd, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
            if (ev.type == EV_REL) {
                if (ev.code == REL_X) {
                    mouse_x += ev.value;
                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_x >= (int)fb.vinfo.xres) mouse_x = fb.vinfo.xres - 1;
                } else if (ev.code == REL_Y) {
                    mouse_y += ev.value;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_y >= (int)fb.vinfo.yres) mouse_y = fb.vinfo.yres - 1;
                }
            } else if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
                running = false; // Exit on left mouse click
            }
        }

        // Clear screen and draw rectangle
        clear_framebuffer(fb, black);
        draw_rectangle(fb, mouse_x - rect_width / 2, mouse_y - rect_height / 2, rect_width, rect_height, green);

        // Cap frame rate
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    // Cleanup
    munmap(fb.buffer, fb.size);
    close(fb.fd);
    close(mouse_fd);
    return 0;
}
#endif

#include <iostream>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstring>

#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/fb.h>

// Function to check if a key is pressed (non-blocking)
bool kbhit()
{
    struct termios oldt, newt;
    int ch;
    int oldf;

    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch != EOF)
    {
        ungetc(ch, stdin);
        return true;
    }
    return false;
}

struct Framebuffer
{
    int fd;
    uint8_t *buffer;
    size_t size;
    fb_var_screeninfo vinfo;
    fb_fix_screeninfo finfo;
};

void drawPixel(const Framebuffer &framebuffer, unsigned int x, unsigned int y, uint32_t color)
{
    if (x > framebuffer.vinfo.xres ||
        y > framebuffer.vinfo.yres)
    {
        return;
    }

    uint32_t *buffer = reinterpret_cast<uint32_t *>(framebuffer.buffer + (y * framebuffer.finfo.line_length + x * 4));
    *buffer = color;
}

void drawHLine(const Framebuffer &framebuffer, unsigned int x, unsigned int y, unsigned int length, uint32_t color)
{
    if (x + length > framebuffer.vinfo.xres ||
        y > framebuffer.vinfo.yres)
    {
        return;
    }

    uint32_t *buffer = reinterpret_cast<uint32_t *>(framebuffer.buffer + (y * framebuffer.finfo.line_length + x * 4));
    for (int i = 0; i < length; ++i)
    {
        *buffer++ = color;
    }
}

bool initFramebuffer(Framebuffer &framebuffer)
{
    framebuffer.fd = open("/dev/fb0", O_RDWR);

    if (ioctl(framebuffer.fd, FBIOGET_VSCREENINFO, &framebuffer.vinfo) == -1)
    {
        std::cout << "Error reading variable screen info" << std::endl;
        close(framebuffer.fd);
        return false;
    }

    if (ioctl(framebuffer.fd, FBIOGET_FSCREENINFO, &framebuffer.finfo) == -1)
    {
        std::cout << "Error reading fixed screen info" << std::endl;
        close(framebuffer.fd);
        return false;
    }

    std::cout << "Buffer resolution " << framebuffer.vinfo.xres << "x" << framebuffer.vinfo.yres << std::endl;

    framebuffer.size = framebuffer.vinfo.yres_virtual * framebuffer.finfo.line_length;
    framebuffer.buffer = (uint8_t *)mmap(0, framebuffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, framebuffer.fd, 0);
    if (framebuffer.buffer == nullptr)
    {
        std::cout << "Error: failed to map framebuffer device to memory" << std::endl;
        close(framebuffer.fd);
        return false;
    }

    return true;
}

int main()
{
    Framebuffer framebuffer;

    if (!initFramebuffer(framebuffer))
    {
        return 1;
    }

    int offset = 0;

    while (!kbhit())
    {
        std::cout << "foo" << std::endl;
        usleep(500000);

        memset(framebuffer.buffer, 0, framebuffer.size);

        drawHLine(framebuffer, offset, 0, 10, 0xff0000);  // red
        drawHLine(framebuffer, offset, 10, 20, 0x00ff00); // green
        drawHLine(framebuffer, offset, 20, 30, 0x0000ff); // blue

        offset +=5;
    }

    // clear the overlay before exit
    memset(framebuffer.buffer, 0, framebuffer.size);
    munmap(framebuffer.buffer, framebuffer.size);
    close(framebuffer.fd);

    return 0;
}
