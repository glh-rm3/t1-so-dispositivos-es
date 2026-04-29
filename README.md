# Trabalho de Sistemas Operacionais - Dispositivos de E/S

# 🔑 Keylogger — Trabalho Acadêmico

> **Disciplina:** Sistemas Operacionais  
> **Curso:** Bacharelado em Sistemas de Informação  
> **Instituição:** Universidade Estadual do Mato Grosso do Sul (UEMS)  
> **Tema:** Dispositivos de Entrada e Saída

---

## 📋 Descrição

Este projeto implementa um **Keylogger** em linguagem C que captura, em tempo real, as teclas digitadas pelo usuário diretamente a partir do subsistema de entrada do Kernel Linux. O programa foi desenvolvido com fins estritamente acadêmicos para demonstrar, na prática, como o Kernel Linux trata dispositivos de entrada e saída como arquivos, e como syscalls como `read` e `write` podem ser utilizadas para interagir com esses dispositivos via **file descriptors**.

> ⚠️ **Aviso:** Este software foi desenvolvido exclusivamente para fins educacionais, no contexto da disciplina de Sistemas Operacionais. O uso desta ferramenta fora do ambiente acadêmico pode violar leis de privacidade e segurança da informação.

---

## 🐧 Compatibilidade

Este programa é **compatível exclusivamente com distribuições GNU/Linux**.

A razão é fundamental: o Kernel Linux expõe os dispositivos de entrada (teclado, mouse, etc.) como arquivos no diretório `/dev/input/`, na forma de `eventX` (onde `X` é o número do evento). Essa abstração, característica do design Unix/Linux de "tudo é um arquivo", permite que o programa abra o dispositivo de teclado com um `open()` comum e leia os eventos de teclas com `read()`, da mesma forma que leria qualquer arquivo regular.

---

## 🛠️ Bibliotecas Utilizadas

| Biblioteca | Finalidade |
|---|---|
| `<fcntl.h>` | Abertura de arquivos/dispositivos (`open`, `O_RDONLY`, `O_WRONLY`, etc.) |
| `<unistd.h>` | Syscalls de baixo nível (`read`, `write`, `close`) |
| `<string.h>` | Manipulação de strings (`strlen`, `strcpy`, `strncpy`) |
| `<ctype.h>` | Conversão de caracteres (`tolower`) |
| `<stdbool.h>` | Suporte ao tipo `bool` em C |
| `<linux/input.h>` | Estruturas e constantes do subsistema de entrada do Kernel (`input_event`, `EV_KEY`, `LED_CAPSL`, etc.) |
| `<sys/ioctl.h>` | Chamadas de controle de dispositivo (`ioctl`, `EVIOCGNAME`, `EVIOCGBIT`, `EVIOCGLED`) |
| `"keymap.h"` | Mapeamento customizado de key codes para strings legíveis |

---

## 🗂️ Estrutura dos Arquivos

```
.
├── include/
│   └── keymap.h         # Tabela de mapeamento de key codes
├── src/
│   ├── main.c           # Lógica principal: captura de teclas e escrita no log
│   └── scan_device.c    # Scanner automático do dispositivo de teclado
├── .log.txt             # Arquivo oculto de saída (gerado em tempo de execução)
├── LICENSE
└── Makefile
```

---

## ⚙️ Como Funciona

### 1. Scanner de Dispositivo (`scan_device.c`)

O programa não depende de um caminho de dispositivo fixo (como `/dev/input/event3`), pois o número do evento pode variar entre distribuições e configurações de hardware. Para resolver isso, a função `scan_device()` percorre todos os eventos disponíveis em `/dev/input/event0` até `event31` e aplica uma série de filtros para identificar o teclado real:

1. **Filtragem por nome:** Descarta dispositivos cujo nome contenha "mouse", "pointer", "trackpad" ou "touchpad".
2. **Requer "keyboard" ou "kbd" no nome:** Somente dispositivos identificados explicitamente como teclados passam adiante.
3. **Descarta EV_REL:** Eventos de movimento relativo (típicos de mouses) são excluídos, mesmo que o dispositivo se anuncie como teclado.
4. **Verifica EV_KEY + KEY_Q:** Confirma suporte a teclas alfanuméricas.
5. **Verifica EV_LED + LED_CAPSL:** O suporte ao LED de Caps Lock é o critério mais discriminante, mouses com teclas extras geralmente não possuem esse recurso, eliminando falsos positivos.

> **Nota técnica:** Durante os testes, foi verificado que mouses modernos com botões programáveis podem se anunciar ao Kernel como dispositivos de teclado. A presença do LED de Caps Lock foi o critério mais eficaz para eliminar esses falsos positivos.

### 2. Captura de Teclas (`main.c`)

Com o file descriptor do teclado em mãos, o programa entra em um loop bloqueante onde a syscall `read()` aguarda por novos eventos do tipo `struct input_event` sem consumir CPU:

```c
ssize_t bytes = read(fd, &ev, sizeof(ev));
```

A estrutura `input_event` fornecida pelo Kernel contém três campos relevantes:

- `ev.type` — O tipo do evento (ex: `EV_KEY` para teclas, `EV_LED` para LEDs).
- `ev.code` — O código da tecla pressionada (ex: `KEY_A`, `KEY_ENTER`).
- `ev.value` — O estado: `1` (pressionada), `0` (solta), `2` (repetição por segurar).

### 3. Controle de Estado (Shift e Caps Lock)

Para reconstruir o caractere correto que o usuário digitou, é necessário rastrear dois estados:

- **Shift:** Monitorado pelos eventos de `KEY_LEFTSHIFT` e `KEY_RIGHTSHIFT` (value `1` = pressionado, `0` = solto).
- **Caps Lock:** Detectado de duas formas:
  - **Na inicialização:** Uma chamada `ioctl(fd, EVIOCGLED(...))` consulta o estado atual dos LEDs do teclado, sincronizando o programa com o estado real antes do primeiro evento.
  - **Em tempo de execução:** O Kernel emite eventos `EV_LED / LED_CAPSL` sempre que o estado do Caps Lock muda, garantindo sincronia contínua sem necessidade de polling.

A lógica para determinar se o caractere deve ser maiúsculo utiliza um XOR:

```c
bool should_uppercase = (shift_pressed != caps_lock_on);
```

Isso reflete o comportamento real do teclado: Shift ativa maiúsculas quando Caps Lock está desligado, e as desativa quando Caps Lock está ligado.

### 4. Mapeamento de Teclas (`keymap.h`)

O arquivo `keymap.h` define o array `key_code_names[]`, que mapeia cada `KEY_CODE` definido em `<linux/input.h>` para uma string legível. Teclas especiais são representadas com marcadores entre underscores, por exemplo:

| Tecla | Representação no log |
|---|---|
| Shift Esquerdo | `_lsf_` |
| Shift Direito | `_rsf_` |
| Enter | `\n` (nova linha) |
| Caps Lock | `_caps_` |
| Tab | `_tab_` |
| Seta para cima | `_up_` |

> **Limitação conhecida:** Caracteres que dependem de combinações com Shift para produzir um símbolo diferente (como `Shift+2` para `@` em layouts ABNT2) não são resolvidos para o símbolo final. O log registrará a sequência de teclas, por exemplo `_lsf_2`. Isso ocorre porque o mapeamento é feito no nível de key codes, sem interpretação de layout de teclado.

**Array key_code_names:** O array `key_code_names[]` mapeia cada key code para uma string legível. Os códigos utilizados foram definidos com base no arquivo de cabeçalho do Kernel Linux:

```bash
/usr/include/linux/input-event-codes.h
```
Este arquivo faz parte da biblioteca `<linux/input.h>` e é a fonte oficial dos códigos de teclas (`KEY_*`), tipos de evento (`EV_*`), LEDs (`LED_*`) e demais constantes utilizadas pelo subsistema de entrada do Kernel.

### 5. Escrita no Log

Cada tecla capturada é gravada imediatamente no arquivo oculto `.log.txt` por meio da syscall `write()`, sem buffers intermediários:

```c
write(log_fd, _key, strlen(_key));
```

O arquivo é aberto com as flags `O_WRONLY | O_APPEND | O_CREAT`, garantindo que o conteúdo seja adicionado ao final a cada execução e que o arquivo seja criado automaticamente caso não exista.

---

## 🔍 Identificando o Dispositivo com `evtest`

Para inspecionar manualmente os dispositivos de entrada disponíveis no sistema, utilize o utilitário `evtest`:

**Instalação:**

```bash
# Debian/Ubuntu e derivados
sudo apt install evtest

# Fedora e derivados Red Hat
sudo dnf install evtest

# Arch Linux
sudo pacman -S evtest
```

**Uso:**

```bash
sudo evtest
```

O programa listará todos os eventos disponíveis com seus respectivos números e nomes de dispositivo. Isso permite verificar em qual `eventX` o teclado está registrado no seu sistema.

> **Observação:** Em grande parte das distribuições, o teclado é mapeado como `event3`. Em testes realizados no Fedora, o evento correto foi o `event6`. O scanner automático do programa lida com essa variação sem necessidade de configuração manual.

---

## 🚀 Compilação e Execução

### Com make (recomendado)

O projeto inclui um Makefile que automatiza a compilação. É necessário ter o utilitário `make` instalado.

```bash
# Debian/Ubuntu
sudo apt install make

# Fedora
sudo dnf install make

# Arch Linux
sudo pacman -S make

# Compilar
make

# Remover arquivos objeto e o binário gerado
make clean
```

### Sem make (compilação manual)

Caso prefira não instalar o `make`, é possível compilar diretamente com `cc` ou `gcc`:

```bash
gcc -c src/main.c        -I./include
gcc -c src/scan_device.c -I./include
gcc main.o scan_device.o -o main

# Executar (requer permissão de leitura em /dev/input/eventX)
sudo ./main
```

> **Permissões:** O acesso direto a `/dev/input/eventX` geralmente requer privilégios de superusuário (`sudo`) ou que o usuário pertença ao grupo `input`.

---

## 📚 Conceitos de Sistemas Operacionais Abordados

- **Abstração de dispositivos como arquivos:** O Kernel Linux representa dispositivos de entrada no sistema de arquivos virtual (`/dev/input/`), permitindo o uso das syscalls padrão de I/O.
- **Syscalls `read` e `write`:** Utilizadas para ler eventos do dispositivo e escrever no arquivo de log, demonstrando a interface entre o espaço do usuário e o Kernel.
- **File Descriptors:** Identificadores inteiros que referenciam recursos de I/O abertos, usados para gerenciar o dispositivo de teclado e o arquivo de log.
- **`ioctl` (Input/Output Control):** Syscall para operações de controle específicas de dispositivo, utilizada para consultar o nome, as capacidades e o estado dos LEDs do dispositivo.
- **Eventos de entrada do Kernel (`input_event`):** Estrutura padronizada pelo Kernel para reportar eventos de qualquer dispositivo de entrada, independente do tipo.
- **I/O bloqueante:** O `read()` sobre um file descriptor de dispositivo suspende o processo até que um novo evento esteja disponível, eliminando busy-waiting.
