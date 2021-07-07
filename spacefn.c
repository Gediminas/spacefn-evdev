/*
 * spacefn-evdev.c
 * James Laird-Wah (abrasive) 2018
 * This code is in the public domain.
 */

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

enum {
    IDLE,
    DECIDE,
    SHIFT,
} state = IDLE;

enum {
    LAYER_STD,
    LAYER_SPC,
    LAYER_DOT,
} layer = LAYER_STD;

struct libevdev *idev;
struct libevdev_uinput *odev;

int write_log(const char *format, ...)
{
    struct timeval tmnow;
    struct tm *tm;
    char buf[30], usec_buf[6];
    gettimeofday(&tmnow, NULL);
    tm = localtime(&tmnow.tv_sec);
    strftime(buf, 30, "%Y-%m-%d %H:%M:%S", tm);
    printf("\e[0;37m%s.%d\e[0m ", buf, tmnow.tv_usec);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
unsigned int key_map_modifier(unsigned int code, int vendor) {
    const bool bApple = (vendor == 1452 || vendor == 76);
    //printf("%d => ", code);
    switch (code) {
        case KEY_LEFTCTRL:  return KEY_LEFTMETA;
        case KEY_LEFTALT:   return bApple ? KEY_LEFTALT  : KEY_LEFTCTRL;
        case KEY_LEFTMETA:  return bApple ? KEY_LEFTCTRL : KEY_LEFTALT;
        case KEY_RIGHTMETA: return KEY_RIGHTSHIFT; //Apple CMD
        case KEY_RIGHTALT:  return KEY_RIGHTSHIFT; //ThinkPad AltGr
        case KEY_CAPSLOCK:  return KEY_ESC;
        case KEY_SYSRQ:     return KEY_RIGHTALT;   //ThinkPad PrtSc
    }
    //printf("%d (%d)\n", code, KEY_SYSRQ);
    return code;
}

unsigned int key_map_spc(unsigned int code, int layer, int vendor, bool *bAlt, bool *bCtrl, bool *bShift) {
    switch (code) {
        case KEY_BRIGHTNESSDOWN: exit(0);   // my magical escape button

        case KEY_1:           return KEY_F1;
        case KEY_2:           return KEY_F2;
        case KEY_3:           return KEY_F3;
        case KEY_4:           return KEY_F4;
        case KEY_5:           return KEY_F5;
        case KEY_6:           return KEY_F6;
        case KEY_7:           return KEY_F7;
        case KEY_8:           return KEY_F8;
        case KEY_9:           return KEY_F9;
        case KEY_0:           return KEY_F10;
        case KEY_MINUS:       return KEY_F11;
        case KEY_EQUAL:       return KEY_F12;

        case KEY_W:           *bCtrl = true; return KEY_S;
        case KEY_E:           *bCtrl = true; return KEY_TAB;
        case KEY_T:           return KEY_PAGEUP;
        case KEY_G:           return KEY_PAGEDOWN;

        case KEY_A:           *bAlt = *bShift = true; return KEY_TAB;
        case KEY_S:           *bAlt = true;           return KEY_TAB;
        case KEY_D:           *bAlt = *bCtrl = true;  return KEY_LEFT;
        case KEY_F:           *bAlt = *bCtrl = true;  return KEY_RIGHT;

        case KEY_X:           *bCtrl = true; return KEY_X;
        case KEY_C:           *bCtrl = true; return KEY_C;
        case KEY_V:           *bCtrl = true; return KEY_V;

        case KEY_H:           return KEY_LEFT;
        case KEY_J:           return KEY_DOWN;
        case KEY_K:           return KEY_UP;
        case KEY_L:           return KEY_RIGHT;
        case KEY_B:           return KEY_ENTER;
        case KEY_N:           return KEY_ESC;
        case KEY_M:           return KEY_BACKSPACE;
        case KEY_Y:           return KEY_SPACE;
        case KEY_U:           *bCtrl = true; return KEY_LEFT;
        case KEY_I:           *bCtrl = true; return KEY_RIGHT;
        case KEY_O:           return KEY_HOME;
        case KEY_P:           return KEY_END;
        case KEY_BACKSPACE:   return KEY_DELETE;
    }
    return 0;
}

unsigned int key_map_dot(unsigned int code, int layer, int vendor, bool *bAlt, bool *bCtrl, bool *bShift) {
    switch (code) {
        case KEY_E:           return KEY_LEFTBRACE;
        case KEY_R:           return KEY_RIGHTBRACE;

        case KEY_D:           *bShift = true; return KEY_9;
        case KEY_F:           *bShift = true; return KEY_0;

        case KEY_X:           *bShift = true; return KEY_LEFTBRACE;
        case KEY_C:           *bShift = true; return KEY_RIGHTBRACE;

        case KEY_H:           return KEY_LEFT;
        case KEY_J:           return KEY_DOWN;
        case KEY_K:           return KEY_UP;
        case KEY_L:           return KEY_RIGHT;
        case KEY_B:           return KEY_ENTER;
        case KEY_N:           return KEY_ESC;
        case KEY_M:           return KEY_BACKSPACE;
        case KEY_Y:           return KEY_SPACE;
        case KEY_U:           *bCtrl = true; return KEY_LEFT;
        case KEY_I:           *bCtrl = true; return KEY_RIGHT;
        case KEY_O:           return KEY_HOME;
        case KEY_P:           return KEY_END;
    }
    return 0;
}

unsigned int key_map(unsigned int code, int layer, int vendor, bool *bAlt, bool *bCtrl, bool *bShift) {
    unsigned int new_code = 0;
    *bAlt = false;
    *bCtrl = false;
    *bShift = false;
    switch (layer) {
        case LAYER_SPC: new_code = key_map_spc(code, layer, vendor, bAlt, bCtrl, bShift); break;
        case LAYER_DOT: new_code = key_map_dot(code, layer, vendor, bAlt, bCtrl, bShift); break;
    }
    //printf("Mapped %d => %d [%c%c%c]\n", code, new_code, *bAlt ? 'A' : '-', *bCtrl ? 'C' : '-', *bShift ? 'S' : '-');
    return new_code;
}

// Blacklist keys for which I have a mapping, to try and train myself out of using them
int blacklist(unsigned int code) {
    // switch (code) {
    //     case KEY_UP:
    //     case KEY_DOWN:
    //     case KEY_RIGHT:
    //     case KEY_LEFT:
    //     case KEY_HOME:
    //     case KEY_END:
    //     case KEY_PAGEUP:
    //     case KEY_PAGEDOWN:
    //         return 1;
    // }
    return 0;
}


// Ordered unique key buffer {{{1
#define MAX_BUFFER 8
unsigned int buffer[MAX_BUFFER];
unsigned int n_buffer = 0;

static int buffer_contains(unsigned int code) {
    for (int i=0; i<n_buffer; i++)
        if (buffer[i] == code)
            return 1;

    return 0;
}

static int buffer_remove(unsigned int code) {
    for (int i=0; i<n_buffer; i++)
        if (buffer[i] == code) {
            memcpy(&buffer[i], &buffer[i+1], (n_buffer - i - 1) * sizeof(*buffer));
            n_buffer--;
            return 1;
        }
    return 0;
}

static int buffer_append(unsigned int code) {
    if (n_buffer >= MAX_BUFFER)
        return 1;
    buffer[n_buffer++] = code;
    return 0;
}

// Key I/O functions {{{1
// output {{{2
#define V_RELEASE 0
#define V_PRESS 1
#define V_REPEAT 2

static void send_key(unsigned int code, int value) {
    libevdev_uinput_write_event(odev, EV_KEY, code, value);
    libevdev_uinput_write_event(odev, EV_SYN, SYN_REPORT, 0);
}

static void send_press(unsigned int code) {
    send_key(code, 1);
}

static void send_release(unsigned int code) {
    send_key(code, 0);
}

static void send_repeat(unsigned int code) {
    send_key(code, 2);
}

// input {{{2
static int read_one_key(struct input_event *ev, int vendor) {
    /* write_log("waiting for key\n"); */
    int err = libevdev_next_event(idev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, ev);
    /* write_log("key: %d\n", ev->code); */
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        exit(1);
    }

    if (ev->type != EV_KEY) {
        libevdev_uinput_write_event(odev, ev->type, ev->code, ev->value);
        return -1;
    }

    ev->code = key_map_modifier(ev->code, vendor);
    /* write_log("key: %d\n", ev->code); */

    if (blacklist(ev->code)) {
        return -1;
    }

    return 0;
}

static void state_idle(int vendor) {  // {{{2
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev, vendor));

        if (ev.code == KEY_SPACE && ev.value == V_PRESS) {
            state = DECIDE;
            layer = LAYER_SPC;
            return;
        }

        if (ev.code == KEY_DOT && ev.value == V_PRESS) {
            state = DECIDE;
            layer = LAYER_DOT;
            return;
        }

        /* write_log("key: %x\n", ev->code); */
        send_key(ev.code, ev.value);
    }
}

static void state_decide(int vendor, int fd) {    // {{{2
    n_buffer = 0;
    struct input_event ev;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    fd_set set;
    FD_ZERO(&set);

    while (timeout.tv_usec >= 0) {
        FD_SET(fd, &set);
        int nfds = select(fd+1, &set, NULL, NULL, &timeout);
        if (!nfds)
            break;

        while (read_one_key(&ev, vendor));

        if (ev.value == V_PRESS) {
            buffer_append(ev.code);
            continue;
        }

        if (ev.code == KEY_SPACE && ev.value == V_RELEASE) {
            send_key(KEY_SPACE, V_PRESS);
            send_key(KEY_SPACE, V_RELEASE);
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_PRESS);
            state = IDLE;
            return;
        }

        if (ev.code == KEY_DOT && ev.value == V_RELEASE) {
            send_key(KEY_DOT, V_PRESS);
            send_key(KEY_DOT, V_RELEASE);
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_PRESS);
            state = IDLE;
            return;
        }

        if (ev.value == V_RELEASE && !buffer_contains(ev.code)) {
            send_key(ev.code, ev.value);
            continue;
        }

        if (ev.value == V_RELEASE && buffer_remove(ev.code)) {
            bool bAlt = false;
            bool bCtrl = false;
            bool bShift = false;
            unsigned int code = key_map(ev.code, layer, vendor, &bAlt, &bCtrl, &bShift);
            //printf("SPACEcode1: %d - ctrl %d,  press %d,  release %d\n", code, bCtrl, V_PRESS, V_RELEASE);
            if (bAlt)   { send_key(KEY_LEFTALT,   V_PRESS); }
            if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_PRESS); }
            if (bShift) { send_key(KEY_LEFTSHIFT, V_PRESS); }
            send_key(code, V_PRESS);
            send_key(code, V_RELEASE);
            if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
            if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
            if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
            state = SHIFT;
            return;
        }
    }

    printf("timed out\n");
    for (int i=0; i<n_buffer; i++) {
        bool bAlt = false;
        bool bCtrl = false;
        bool bShift = false;
        unsigned int code = key_map(buffer[i], layer, vendor, &bAlt, &bCtrl, &bShift);
        //printf("SPACEcode2: %d - ctrl %d\n", code, bCtrl);
        if (!code) {
            code = buffer[i];
        }
        if (bAlt)   { send_key(KEY_LEFTALT,   V_PRESS); }
        if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_PRESS); }
        if (bShift) { send_key(KEY_LEFTSHIFT, V_PRESS); }
        send_key(code, V_PRESS);
        if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
        if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
        if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
    }
    state = SHIFT;
}

static void state_shift(int vendor) {
    n_buffer = 0;
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev, vendor));

        if (ev.code == KEY_SPACE && ev.value == V_RELEASE) {
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_RELEASE);
            state = IDLE;
            return;
        }
        if (ev.code == KEY_SPACE)
            continue;

        if (ev.code == KEY_DOT && ev.value == V_RELEASE) {
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_RELEASE);
            state = IDLE;
            return;
        }
        if (ev.code == KEY_DOT)
            continue;

        bool bAlt = false;
        bool bCtrl = false;
        bool bShift = false;
        unsigned int code = key_map(ev.code, layer, vendor, &bAlt, &bCtrl, &bShift);
        //printf("SPACEcode3: %d - ctrl %d\n", code, bCtrl);
        if (code) {
            if (ev.value == V_PRESS) {
                buffer_append(code);
            }
            else if (ev.value == V_RELEASE) {
                buffer_remove(code);
            }

            if (ev.value == V_PRESS) {
                if (bAlt)   { send_key(KEY_LEFTALT,   V_PRESS); }
                if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_PRESS); }
                if (bShift) { send_key(KEY_LEFTSHIFT, V_PRESS); }
            }

            send_key(code, ev.value);

            if (ev.value == V_PRESS) {
                if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
                if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
                if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
            }
        } else {
            send_key(ev.code, ev.value);
        }

    }
}

static void run_state_machine(int fd, int vendor) {
    for (;;) {
        printf("state %d\n", state);
        switch (state) {
            case IDLE:
                state_idle(vendor);
                break;
            case DECIDE:
                state_decide(vendor, fd);
                break;
            case SHIFT:
                state_shift(vendor);
                break;
        }
    }
}


int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s /dev/input/...", argv[0]);
        return 1;
    }

    const char* interface = argv[1];
    /* printf("\e[1;33m* Interface: %s\e[0m\n", interface); */

    const int fd = open(interface, O_RDONLY);
    if (fd < 0) {
        perror("open input");
        return 1;
    }
    int err = libevdev_new_from_fd(fd, &idev);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        return 1;
    }

    const char* name = libevdev_get_name(idev);
    const int vendor = libevdev_get_id_vendor(idev);
    printf("\n\e[0;33m** %s => vendor %d => %s\e[0m\n", interface, vendor, name);

    int uifd = open("/dev/uinput", O_RDWR);
    if (uifd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    err = libevdev_uinput_create_from_device(idev, uifd, &odev);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        return 1;
    }

    err = libevdev_grab(idev, LIBEVDEV_GRAB);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        return 1;
    }

    run_state_machine(fd, vendor);
}
