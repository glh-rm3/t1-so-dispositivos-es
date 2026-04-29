#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>
#include <sys/ioctl.h>

int scan_device(char *device_path, size_t size) {
    int i = 0;
    int fd = -1;

    unsigned char ev_bits[EV_MAX / 8 + 1];
    unsigned char key_bits[KEY_MAX / 8 + 1];
    unsigned char led_bits[LED_MAX / 8 + 1];
    char name[256];

    while (i < 32) {
        snprintf(device_path, size, "/dev/input/event%d", i);
        fd = open(device_path, O_RDONLY);

        if (fd >= 0) {
            memset(ev_bits,  0, sizeof(ev_bits));
            memset(key_bits, 0, sizeof(key_bits));
            memset(led_bits, 0, sizeof(led_bits));
            memset(name,     0, sizeof(name));

            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
                close(fd); fd = -1; i++; continue;
            }

            char name_lower[256];
            strncpy(name_lower, name, sizeof(name_lower));
            for (int j = 0; name_lower[j]; j++)
                if (name_lower[j] >= 'A' && name_lower[j] <= 'Z')
                    name_lower[j] += 32;

            if (strstr(name_lower, "mouse")    ||
                strstr(name_lower, "pointer")  ||
                strstr(name_lower, "trackpad") ||
                strstr(name_lower, "touchpad")) {
                close(fd); fd = -1; i++; continue;
            }

            if (!strstr(name_lower, "keyboard") && !strstr(name_lower, "kbd")) {
                close(fd); fd = -1; i++; continue;
            }

            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
                close(fd); fd = -1; i++; continue;
            }

            // Descarta se tiver EV_REL (mouse/ponteiro disfarçado de teclado)
            if (ev_bits[EV_REL / 8] & (1 << (EV_REL % 8))) {
                close(fd); fd = -1; i++; continue;
            }

            // Verifica EV_KEY + KEY_Q + EV_LED + LED_CAPSL
            if (ev_bits[EV_KEY / 8] & (1 << (EV_KEY % 8))) {
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
                    if (key_bits[KEY_Q / 8] & (1 << (KEY_Q % 8))) {
                        if (ev_bits[EV_LED / 8] & (1 << (EV_LED % 8))) {
                            if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(led_bits)), led_bits) >= 0) {
                                if (led_bits[LED_CAPSL / 8] & (1 << (LED_CAPSL % 8))) {
                                    //printf("DEBUG: %s (%s)\n", device_path, name);
                                    return fd;
                                }
                            }
                        }
                    }
                }
            }

            close(fd); fd = -1;
        }
        i++;
    }
    return -1;
}
