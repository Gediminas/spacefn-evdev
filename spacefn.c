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
#include <sys/dir.h>
#include <unistd.h>

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

static int _log(const char *format, ...) {
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
static unsigned int key_map_modifier(unsigned int code, bool is_apple) {
    //printf("%d => ", code);
    switch (code) {
        case KEY_LEFTCTRL:  return KEY_LEFTMETA;                         //Ctrl
        case KEY_LEFTMETA:  return is_apple ? KEY_LEFTCTRL : KEY_LEFTALT;  //Win
        case KEY_LEFTALT:   return is_apple ? KEY_LEFTALT  : KEY_LEFTCTRL; //Alt

        case KEY_RIGHTMETA: return KEY_RIGHTSHIFT; //Apple CMD
        case KEY_RIGHTALT:  return KEY_RIGHTSHIFT; //ThinkPad AltGr
        case KEY_CAPSLOCK:  return KEY_ESC;
        case KEY_SYSRQ:     return KEY_RIGHTALT;   //ThinkPad PrtSc

        //case KEY_RIGHTALT:  return KEY_RIGHTSHIFT; //ThinkPad AltGr
        //case KEY_CAPSLOCK:  return KEY_LEFTCTRL;
    }
    //printf("%d (%d)\n", code, KEY_SYSRQ);
    return code;
}

static unsigned int key_map_spc(unsigned int code, int layer, bool is_apple, bool *bAlt, bool *bCtrl, bool *bShift, bool *bSuper) {
    switch (code) {
        case KEY_BRIGHTNESSDOWN: exit(0);   // some magical escape button?

        //case KEY_1:           *bSuper = true; return KEY_1;
        //case KEY_2:           *bSuper = true; return KEY_2;
        //case KEY_3:           *bSuper = true; return KEY_3;
        //case KEY_4:           *bSuper = true; return KEY_4;
        //case KEY_5:           *bSuper = true; return KEY_5;
        //case KEY_6:           *bSuper = true; return KEY_6;
        //case KEY_7:           *bSuper = true; return KEY_7;
        //case KEY_8:           *bSuper = true; return KEY_8;
        //case KEY_9:           *bSuper = true; return KEY_9;
        //case KEY_0:           *bSuper = true; return KEY_0;
        //case KEY_MINUS:       *bSuper = true; return KEY_MINUS;
        //case KEY_EQUAL:       *bSuper = true; return KEY_EQUAL;

        case KEY_W:           *bCtrl = true; return KEY_S;
        //case KEY_E:           *bCtrl = true; return KEY_TAB;
        case KEY_T:           return KEY_PAGEUP;
        case KEY_G:           return KEY_PAGEDOWN;

        case KEY_X:           *bCtrl = true; return KEY_X;
        case KEY_C:           *bCtrl = true; return KEY_C;
        case KEY_V:           *bCtrl = true; return KEY_V;

        case KEY_BACKSPACE:   return KEY_DELETE;
        //case KEY_TAB:         *bSuper = true; return KEY_TAB;

        case KEY_Y:           return KEY_SPACE;
        case KEY_U:           *bCtrl = true; return KEY_LEFT;
        case KEY_I:           *bCtrl = true; return KEY_RIGHT;
        case KEY_O:           return KEY_HOME;
        case KEY_P:           return KEY_END;

        case KEY_H:           return KEY_LEFT;
        case KEY_J:           return KEY_DOWN;
        case KEY_K:           return KEY_UP;
        case KEY_L:           return KEY_RIGHT;

        case KEY_B:           return KEY_ENTER;
        case KEY_N:           return KEY_ESC;
        case KEY_M:           return KEY_BACKSPACE;
        case KEY_COMMA:       *bCtrl = true; return KEY_BACKSPACE;
    }

    *bSuper = true;
    return code;
}

unsigned int key_map_dot(unsigned int code, int layer, bool is_apple, bool *bAlt, bool *bCtrl, bool *bShift, bool *bSuper) {
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

unsigned int key_map(unsigned int code, int layer, bool is_apple, bool *bAlt,
                     bool *bCtrl, bool *bShift, bool *bSuper) {
    unsigned int new_code = 0;
    *bAlt = false;
    *bCtrl = false;
    *bShift = false;
    *bSuper = false;
    switch (layer) {
        case LAYER_SPC: new_code = key_map_spc(code, layer, is_apple, bAlt, bCtrl, bShift, bSuper); break;
        case LAYER_DOT: new_code = key_map_dot(code, layer, is_apple, bAlt, bCtrl, bShift, bSuper); break;
    }
    // printf("Mapped %d => %d [%c%c%c]\n", code, new_code, *bAlt ? 'A' : '-',
    // *bCtrl ? 'C' : '-', *bShift ? 'S' : '-');
    return new_code;
}

// Blacklist keys for which I have a mapping, to try and train myself out of
// using them
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

static void print_event(struct input_event *ev) {
    printf("Event: %s %s %d\n",
           libevdev_event_type_get_name(ev->type),
           libevdev_event_code_get_name(ev->type, ev->code),
           ev->value);
}

// input {{{2
static int read_one_key(struct input_event *ev, bool is_apple) {
    const int err = libevdev_next_event(idev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, ev);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        exit(1);
    }

    if (ev->type != EV_KEY) {
        libevdev_uinput_write_event(odev, ev->type, ev->code, ev->value);
        return -1;
    }

    ev->code = key_map_modifier(ev->code, is_apple);

    if (blacklist(ev->code)) {
        return -1;
    }

    return 0;
}

// Change buffer from decide state raw keys to shift state mapped keys.
// Just clearing the buffer on decide -> shift change can lead to presses without
// a release sometimes and that can lock a laptop trackpad.
static void fix_buffer(int layer, bool is_apple) {
    unsigned int tbuffer[MAX_BUFFER];
    int moves = 0;
    for (int i=0; i<n_buffer; i++) {
        bool bAlt = false;
        bool bCtrl = false;
        bool bShift = false;
        bool bSuper = false;
        unsigned int code = key_map(buffer[i], layer, is_apple, &bAlt, &bCtrl, &bShift, &bSuper);
        if (!code) {
            code = buffer[i];
        } else {
            tbuffer[moves++] = code;
        }
        if (bAlt)   { send_key(KEY_LEFTALT,   V_PRESS); }
        if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_PRESS); }
        if (bShift) { send_key(KEY_LEFTSHIFT, V_PRESS); }
        if (bSuper) { send_key(KEY_LEFTMETA,  V_PRESS); }
        send_key(code, V_PRESS);
        if (bSuper) { send_key(KEY_LEFTMETA,  V_RELEASE); }
        if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
        if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
        if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
    }
    n_buffer = moves;
    if (n_buffer > 0) memcpy(buffer, tbuffer, n_buffer * sizeof(*buffer));
}


static void state_idle(bool is_apple) {  // {{{2
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev, is_apple));

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

        send_key(ev.code, ev.value);
    }
}

static void state_decide(bool is_apple, int fd) {    // {{{2
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

        while (read_one_key(&ev, is_apple));

        if (ev.value == V_PRESS) {
            buffer_append(ev.code);
            continue;
        }

        if (ev.code == KEY_SPACE && ev.value == V_RELEASE) {
            send_key(KEY_SPACE, V_PRESS);
            send_key(KEY_SPACE, V_RELEASE);
            // These weren't mapped, so send the actual presses and clear the buffer.
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_PRESS);
            n_buffer = 0;
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
            bool bSuper = false;

            const unsigned int code = key_map(ev.code, layer, is_apple, &bAlt, &bCtrl, &bShift, &bSuper);

            if (bAlt)   { send_key(KEY_LEFTALT,   V_PRESS); }
            if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_PRESS); }
            if (bShift) { send_key(KEY_LEFTSHIFT, V_PRESS); }
            if (bSuper) { send_key(KEY_LEFTMETA,  V_PRESS); }
            if (code) {
                send_key(code, V_PRESS);
                send_key(code, V_RELEASE);
            } else {
                send_key(ev.code, V_PRESS);
                send_key(ev.code, V_RELEASE);
            }
            if (bSuper) { send_key(KEY_LEFTMETA,  V_RELEASE); }
            if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
            if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
            if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
            state = SHIFT;
            fix_buffer(layer, is_apple);
            return;
        }
    }

    //printf("timed out\n");
    fix_buffer(layer, is_apple);
    state = SHIFT;
}

static void state_shift(bool is_apple) {
    //n_buffer = 0;
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev, is_apple));

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
        bool bSuper = false;
        unsigned int code = key_map(ev.code, layer, is_apple, &bAlt, &bCtrl, &bShift, &bSuper);
        // printf("SPACEcode3: %d - ctrl %d\n", code, bCtrl);
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
                if (bSuper) { send_key(KEY_LEFTMETA,  V_PRESS); }
            }

            send_key(code, ev.value);

            if (ev.value == V_PRESS) {
                if (bSuper) { send_key(KEY_LEFTMETA,  V_RELEASE); }
                if (bShift) { send_key(KEY_LEFTSHIFT, V_RELEASE); }
                if (bCtrl)  { send_key(KEY_LEFTCTRL,  V_RELEASE); }
                if (bAlt)   { send_key(KEY_LEFTALT,   V_RELEASE); }
            }
        } else {
            send_key(ev.code, ev.value);
        }

    }
}

static void run_state_machine(int fd, bool is_apple) {
    for (;;) {
        //printf("state %d\n", state);
        switch (state) {
            case IDLE:
                state_idle(is_apple);
                break;
            case DECIDE:
                state_decide(is_apple, fd);
                break;
            case SHIFT:
                state_shift(is_apple);
                break;
        }
    }
}

static int dev_select(const struct direct *entry) {
    if (entry->d_type == DT_CHR) return 1;
    else return 0;
}

static int is_keeb(struct libevdev *idev) {
    if (libevdev_has_event_type(idev, EV_KEY) &&
        libevdev_has_event_type(idev, EV_SYN) &&
        libevdev_get_phys(idev) &&  // This will exclude virtual keyboards (like another spacefn instance).
        libevdev_has_event_code(idev, EV_KEY, KEY_SPACE) &&
        libevdev_has_event_code(idev, EV_KEY, KEY_A)) return 1;
    else return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s /dev/input/...", argv[0]);
        return 1;
    }

    const char* interface = argv[1];

    // This sleep is a hack but it gives X time to read the Enter release event
    // when starting (unless someone holds it longer then a second) which keeps
    // several bad things from happening including "stuck" enter and possible locking
    // of a laptop trackpad until another key is pressed- and maybe longer).
    // Not sure who to solve this properly, the enter release will come from
    // spacefn without it and X seems to not connect the press and release in 
    // this case (different logical keyboards).
    sleep(1);

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

    const char* name     = libevdev_get_name(idev);
    const int   vendor   = libevdev_get_id_vendor(idev);
    const int   bustype  = libevdev_get_id_bustype(idev);
    const int   product  = libevdev_get_id_product(idev);
    const int   keeb     = is_keeb(idev);
    const char* phys     = libevdev_get_phys(idev);
    const char* uniq     = libevdev_get_uniq(idev);
    const bool  is_apple  = (NULL != strstr(name, "Apple")) ||
                            (NULL != strstr(phys, "apple")) ||
                            (vendor == 1452) ||
                            (vendor == 76);

    _log("%s: %s, bus: %#x, vendor: %#x, product: %#x, phys: %s)\n", interface, name, bustype, vendor, product, phys);
    _log("Is Apple keyboard? %s\n", is_apple ? "yes" : "no");

    if (!keeb) {
        fprintf(stderr, "This device does not look like a keyboard\n");
        return 1;
    }



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

    run_state_machine(fd, is_apple);
}
