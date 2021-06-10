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

// https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
// Key mapping {{{1
unsigned int key_map(unsigned int code, bool *bCtrl) {
    *bCtrl = false;
    switch (code) {
        case KEY_BRIGHTNESSDOWN:    // my magical escape button
            exit(0);

        case KEY_H:           return KEY_LEFT;
        case KEY_J:           return KEY_DOWN;
        case KEY_K:           return KEY_UP;
        case KEY_L:           return KEY_RIGHT;

        case KEY_B:           return KEY_ENTER;
        case KEY_N:           return KEY_ESC;
        case KEY_M:           return KEY_BACKSPACE;

        case KEY_Y:           return KEY_SPACE;
        case KEY_U:           return KEY_HOME;
        case KEY_I:           return KEY_END;
        case KEY_O:           return KEY_HOME;
        case KEY_P:           return KEY_END;

        case KEY_X:           *bCtrl = true; return KEY_X;
        case KEY_C:           *bCtrl = true; return KEY_C;
        case KEY_V:           *bCtrl = true; return KEY_V;

        case KEY_A:           return KEY_F13;
        case KEY_S:           return KEY_F14;
        case KEY_D:           return KEY_F15;
        case KEY_F:           return KEY_F16;

        case KEY_W:           *bCtrl = true; return KEY_S;
        case KEY_E:           *bCtrl = true; return KEY_TAB;

        //case KEY_SEMICOLON:
        //case KEY_COMMA:       return KEY_PAGEDOWN;
        //case KEY_DOT:         return KEY_PAGEUP;
        //case KEY_SLASH:       return KEY_END;

    }
    return 0;
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


// Global device handles {{{1
struct libevdev *idev;
struct libevdev_uinput *odev;
int fd;

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
static int read_one_key(struct input_event *ev) {
    int err = libevdev_next_event(idev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, ev);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
        exit(1);
    }

    if (ev->type != EV_KEY) {
        libevdev_uinput_write_event(odev, ev->type, ev->code, ev->value);
        return -1;
    }

    if (blacklist(ev->code)) {
        return -1;
    }

    return 0;
}

// State functions {{{1
enum {
    IDLE,
    DECIDE,
    SHIFT,
} state = IDLE;

static void state_idle(void) {  // {{{2
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev));

        if (ev.code == KEY_SPACE && ev.value == V_PRESS) {
            state = DECIDE;
            return;
        }

        send_key(ev.code, ev.value);
    }
}

static void state_decide(void) {    // {{{2
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

        while (read_one_key(&ev));

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

        if (ev.value == V_RELEASE && !buffer_contains(ev.code)) {
            send_key(ev.code, ev.value);
            continue;
        }

        if (ev.value == V_RELEASE && buffer_remove(ev.code)) {
            bool bCtrl = false;
            unsigned int code = key_map(ev.code, &bCtrl);
            /* printf("SPACEcode: %d - ctrl %d,  press %d,  release %d\n", code, bCtrl, V_PRESS, V_RELEASE); */
            if (bCtrl) {
                send_key(KEY_LEFTCTRL, V_PRESS);
            }
            send_key(code, V_PRESS);
            send_key(code, V_RELEASE);
            if (bCtrl) {
                send_key(KEY_LEFTCTRL, V_RELEASE);
            }
            state = SHIFT;
            return;
        }
    }

    printf("timed out\n");
    for (int i=0; i<n_buffer; i++) {
        bool bCtrl = false;
        unsigned int code = key_map(buffer[i], &bCtrl);
        if (!code) {
            code = buffer[i];
        }
        if (bCtrl) {
            send_key(KEY_LEFTCTRL, V_PRESS);
        }
        send_key(code, V_PRESS);
        if (bCtrl) {
            send_key(KEY_LEFTCTRL, V_RELEASE);
        }
    }
    state = SHIFT;
}

static void state_shift(void) {
    n_buffer = 0;
    struct input_event ev;
    for (;;) {
        while (read_one_key(&ev));

        if (ev.code == KEY_SPACE && ev.value == V_RELEASE) {
            for (int i=0; i<n_buffer; i++)
                send_key(buffer[i], V_RELEASE);
            state = IDLE;
            return;
        }
        if (ev.code == KEY_SPACE)
            continue;

        bool bCtrl = false;
        unsigned int code = key_map(ev.code, &bCtrl);
        if (code) {
            if (ev.value == V_PRESS)
                buffer_append(code);
            else if (ev.value == V_RELEASE)
                buffer_remove(code);

            if (bCtrl && ev.value == V_PRESS) {
                send_key(KEY_LEFTCTRL, V_PRESS);
            }
            send_key(code, ev.value);
            if (bCtrl && ev.value == V_RELEASE) {
                send_key(KEY_LEFTCTRL, V_RELEASE);
            }
        } else {
            send_key(ev.code, ev.value);
        }

    }
}

static void run_state_machine(void) {
    for (;;) {
        printf("state %d\n", state);
        switch (state) {
            case IDLE:
                state_idle();
                break;
            case DECIDE:
                state_decide();
                break;
            case SHIFT:
                state_shift();
                break;
        }
    }
}


int main(int argc, char **argv) {   // {{{1
    if (argc < 2) {
        printf("usage: %s /dev/input/...", argv[0]);
        return 1;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open input");
        return 1;
    }

    int err = libevdev_new_from_fd(fd, &idev);
    if (err) {
        fprintf(stderr, "Failed: (%d) %s\n", -err, strerror(err));
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

    run_state_machine();
}
