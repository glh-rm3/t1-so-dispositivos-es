#include <stdio.h>			// snprintf, printf
#include <fcntl.h>			// open, flags de abertura
#include <unistd.h>			// close
#include <string.h>			// memset, strncpy, strstr
#include <linux/input.h>	// Estruturas e constantes do subsistema de entrada
#include <sys/ioctl.h>		// ioctl, comandos de controle

/*
 * Função: scan_device
 * Objetivo: Encontrar automaticamente o dispositivo de teclado físico
 * 
 * Parâmetros:
 * - device_path: buffer onde o caminho do dispositivo será armazenado
 * - size: tamanho do buffer device_path
 * 
 * Retorno:
 * - File descriptor do dispositivo encontrado (>= 0)
 * - -1 se nenhum teclado válido for encontrado
 * 
 * Funcionamento:
 * Percorre /dev/input/event0 até event31 (32 dispositivos possíveis)
 * Aplica uma série de filtros progressivos para eliminar falsos positivos
 * e identificar com precisão o teclado físico real.
 */
int scan_device(char *device_path, size_t size) {
    int i = 0;			// Contador de dispositivos
    int fd = -1;		// File descriptor do dispositivo atual
	
	/*
     * Buffers para armazenar bitmaps de capacidades do dispositivo
     * Cada bit representa uma funcionalidade suportada
     */
    unsigned char ev_bits[EV_MAX / 8 + 1];		// Tipos de evento suportados
    unsigned char key_bits[KEY_MAX / 8 + 1];	// Códigos de tecla suportados
    unsigned char led_bits[LED_MAX / 8 + 1]; 	// LEDs suportados
    char name[256]; // Nome do dispositivo


	/*
     * LOOP DE DESCOBERTA DE DISPOSITIVOS
     * 
     * Itera pelos primeiros 32 dispositivos de evento.
     * 32 é um número razoável - sistemas típicos têm entre 5-15
     * dispositivos de entrada, mas alguns setups podem ter mais.
     */
    while (i < 32) {
        // Monta o caminho: /dev/input/event0, /dev/input/event1, etc.
        snprintf(device_path, size, "/dev/input/event%d", i);

        // Tenta abrir o dispositivo em modo somente leitura
        fd = open(device_path, O_RDONLY);
	
        // Se conseguiu abrir, procede com a análise
        if (fd >= 0) {
			/*
            * Inicializa todos os buffers com zeros
            * Essencial para evitar lixo de memória que poderia
            * causar falsos positivos nas verificações de bits
            */
            memset(ev_bits,  0, sizeof(ev_bits));
            memset(key_bits, 0, sizeof(key_bits));
            memset(led_bits, 0, sizeof(led_bits));
            memset(name,     0, sizeof(name));

			/*
            * FILTRO 1: OBTENÇÃO DO NOME DO DISPOSITIVO
            * 
            * EVIOCGNAME: ioctl que recupera o nome do dispositivo
            * conforme registrado no kernel. Ex: "AT Translated Set 2 keyboard"
            * Se falhar, descarta este dispositivo.
            */

            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
                close(fd); fd = -1; i++; continue;
            }
			
			/*
             * Cria uma versão minúscula do nome para facilitar comparação
             * Converte manualmente cada caractere (evita dependência de locale)
             */
            char name_lower[256];
            strncpy(name_lower, name, sizeof(name_lower));
            for (int j = 0; name_lower[j]; j++)
                if (name_lower[j] >= 'A' && name_lower[j] <= 'Z')
                    name_lower[j] += 32; // Converte para minúscula (ASCII)

			/*
            * FILTRO 2: ELIMINAÇÃO DE DISPOSITIVOS APONTADORES
            * 
            * Descarta explicitamente mouses, touchpads e trackpads
            * que se anunciam como dispositivos de entrada.
            * strstr() retorna NULL se a substring não for encontrada.
            */
            if (strstr(name_lower, "mouse")    ||
                strstr(name_lower, "pointer")  ||
                strstr(name_lower, "trackpad") ||
                strstr(name_lower, "touchpad")) {
                close(fd); fd = -1; i++; continue;
            }
			
		   /*
            * FILTRO 3: EXIGÊNCIA DE IDENTIFICAÇÃO COMO TECLADO
            * 
            * O nome deve conter "keyboard" ou "kbd" (abreviação comum).
            * Isso elimina a maioria dos dispositivos que não são teclados.
            */
            if (!strstr(name_lower, "keyboard") && !strstr(name_lower, "kbd")) {
                close(fd); fd = -1; i++; continue;
            }

	       /*
            * FILTRO 4: VERIFICAÇÃO DOS TIPOS DE EVENTO SUPORTADOS
            * 
            * EVIOCGBIT(0, ...): Obtém o bitmap de tipos de evento suportados.
            * Cada bit representa um EV_* (EV_KEY, EV_REL, EV_ABS, EV_LED, etc.)
            */
            if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0) {
                close(fd); fd = -1; i++; continue;
            }

		   /*
            * FILTRO 5: ELIMINAÇÃO DE DISPOSITIVOS COM EVENTOS RELATIVOS
            * 
            * Dispositivos com EV_REL são tipicamente mouses (movimento relativo).
            * Mesmo que um mouse se anuncie como "keyboard", se ele suporta
            * movimento relativo, não é um teclado real.
            * Isso resolve o problema de mouses "gamer" com macros.
            */
            if (ev_bits[EV_REL / 8] & (1 << (EV_REL % 8))) {
                close(fd); fd = -1; i++; continue;
            }

           /*
            * FILTRO 6: VERIFICAÇÃO DE TECLAS ALFANUMÉRICAS
            * 
            * Confirma que o dispositivo suporta eventos de tecla (EV_KEY)
            * e especificamente a tecla Q (KEY_Q). Se um dispositivo tem
            * a tecla Q, muito provavelmente é um teclado completo.
            */
            if (ev_bits[EV_KEY / 8] & (1 << (EV_KEY % 8))) {
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0) {
                    if (key_bits[KEY_Q / 8] & (1 << (KEY_Q % 8))) {

						/*
                         * FILTRO 7: VERIFICAÇÃO DE LED DE CAPS LOCK
                         * 
                         * Este é o filtro mais discriminante.
                         * Verifica se o dispositivo suporta eventos de LED (EV_LED)
                         * e especificamente o LED de Caps Lock (LED_CAPSL).
                         * 
                         * POR QUE ISSO É EFICAZ:
                         * Teclados físicos praticamente sempre têm LED de Caps Lock.
                         * Mouses com botões programáveis, mesmo que registrados como
                         * teclado, não possuem este LED. É o melhor diferenciador.
                         */
                        if (ev_bits[EV_LED / 8] & (1 << (EV_LED % 8))) {
                            if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(led_bits)), led_bits) >= 0) {
                                if (led_bits[LED_CAPSL / 8] & (1 << (LED_CAPSL % 8))) {
									/*
                                     * DISPOSITIVO VALIDADO!
                                     * Passou por todos os filtros:
                                     * 1. Tem nome
                                     * 2. Não é mouse/touchpad
                                     * 3. É identificado como teclado
                                     * 4. Não tem eventos relativos
                                     * 5. Suporta teclas (EV_KEY)
                                     * 6. Tem a tecla Q
                                     * 7. Suporta LED de Caps Lock
                                     * 
                                     * Retorna o file descriptor aberto e pronto para uso
                                     */
                                    //printf("DEBUG: %s (%s)\n", device_path, name);
                                    return fd;
                                }
                            }
                        }
                    }
                }
            }
           /*
            * Se chegou aqui, o dispositivo não passou em todos os filtros
            * Fecha o file descriptor e continua para o próximo dispositivo
            */
            close(fd); fd = -1;
        }
        i++; // Próximo dispositivo
    }

   /*
    * Se o loop terminou sem encontrar um teclado válido,
    * retorna -1 para indicar falha
    */
    return -1;
}
