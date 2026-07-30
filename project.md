# BLE Remote → USB HID Bridge (Zwift Navigation Remote)

> Ponte que lê um controle de guidão BLE (DQX-Q7) e o reemite ao PC como
> teclado USB HID, permitindo navegar a interface do Zwift (setas / Enter /
> Esc) — recuperando no PC a experiência de "cursor" que o Apple Remote dava
> no Apple TV. As ações in-game continuam sendo feitas pelo Zwift Companion.

---

## 1. Objetivo

Migrei o Zwift do Apple TV para o PC. No Apple TV eu navegava os menus com o
Apple Remote (D-pad + select). No PC perdi esse "controle remoto" da interface.

Este projeto transforma um controle de mídia Bluetooth de guidão em um
**teclado USB de navegação** para o Zwift no PC:

- **4 direcionais** (setas) + **Enter** para navegar/confirmar nos menus.
- **Esc** para voltar/sair (via duplo-toque).
- As ações in-game (power-up, câmera, gestos, mensagens) **permanecem no
  Zwift Companion**, que já uso há anos sem problemas. Não são alvo deste
  projeto.
- Duplo-toque fica disponível como bônus para ações extras futuras.

**Escopo deliberadamente enxuto:** o controle = "Apple Remote para o PC".
Nada além de navegação é obrigatório.

---

## 2. Hardware

### Placa escolhida: ESP32-S3 (DevKit N16R8)
Motivo: é a única peça disponível que tem **BLE central** (para ouvir o
controle) **e USB nativo** (para se apresentar ao PC como teclado HID real),
simultaneamente.

Peças descartadas e por quê:
- **ATmega32U4 (Pro Micro):** tem USB nativo, mas **zero Bluetooth**. Fora.
- **ESP32-WROOM:** tem BLE, mas **sem USB device nativo** (USB via chip serial
  CP2102/CH340). Não vira teclado USB limpo. Fora para este caminho.
- **ESP32-C3:** tem BLE, mas USB é Serial/JTAG, não HID pleno confortável.
  Plano B se necessário.
- **NodeMCU / ESP-01 (ESP8266):** sem Bluetooth. Fora.
- **JY-MCU BT_Board (HC-05/06):** Bluetooth Classic SPP, sem BLE nem HID host.
  Fora.

### O controle: DQX-Q7
- Controle de mídia Bluetooth de guidão/volante, à prova d'água, **5 botões**.
- Fabricante do SoC: **Zhuhai Jieli (JieLi)** — chip BT chinês comum.
- Vendido como "Bluetooth Media Button Remote" para moto/bike (marcas
  revendedoras: Jaesien etc.). Funciona em iOS/Android sem app.
- **BT Address:** `98:4a:c0:ce:bf:a2`
- Alimentação original: pilha **CR2** (lítio 3V não recarregável).

---

## 3. Como o controle se comporta (dados capturados)

### 3.1 Identidade BLE
- Aparece como **BLE** (confirmado no Device Manager: "Dispositivo LE
  Bluetooth"; e no scan como Appearance = HID Keyboard, `0x03C1`).
- **PnP ID = `02-AC-05-2C-02-1B-01`** → Vendor ID `0x05AC` = **Apple**. O
  controle spoofa identidade Apple de propósito, para iOS/tvOS tratá-lo bem.
- `ManufacturerName = zhuhai_jieli`, `ModelNumber = hid_mouse` (string default
  de firmware — **ignorar**, não reflete comportamento real).

### 3.2 Serviços GATT (6 serviços)
- `0x1800` Generic Access
- `0x1801` Generic Attribute
- `0x180A` Device Information
- `0x180F` Battery
- `0xAE40` (proprietário JieLi): char `0xAE41` (write), `0xAE42` (notify/CCCD)
- `0xAE00` (proprietário JieLi): char `0xAE01` (write), `0xAE02` (notify/CCCD)

> **NÃO há serviço HID `0x1812` visível via GATT genérico no iOS.** O iOS
> esconde o HID de apps de terceiros. Mas o HID **existe** e funciona: no
> Windows o controle opera como teclado/consumer HID nativo (volume, play,
> faixas funcionam sem app companion). Os serviços `0xAExx` proprietários,
> quando assinados (AE42/AE02), **não emitiram nada** ao apertar botões —
> os botões saem pelo **HID padrão**, não pelos AExx.

### 3.3 Report HID dos botões (o dado central) — capturado via WebHID
Ferramenta usada: **USB Device Viewer online (WebHID)**
(https://www.codertools.net/tools/usb-device-viewer.php — Chrome/Edge apenas).

```
=== HID Collection ===
Usage Page: 0x000C (Consumer)
Usage:      0x0001 (Consumer Control)
Report ID:  1
Tamanho:    2 bytes (bitmap)
```

**Mapa de botões (bitmap, byte 0):**

| Botão físico      | Valor report | Máscara | Bit |
|-------------------|:------------:|:-------:|:---:|
| Vol+              | `01 00`      | `0x01`  |  0  |
| Vol−              | `02 00`      | `0x02`  |  1  |
| → (avançar)       | `04 00`      | `0x04`  |  2  |
| ← (retroceder)    | `08 00`      | `0x08`  |  3  |
| Play/Pause        | `10 00`      | `0x10`  |  4  |

- Cada toque envia `<valor> 00` seguido **imediatamente** de `00 00` (release).
- `00 00` = todos os botões soltos (é um bitmap posicional, não "usage por vez").
- Teste "está pressionado?": `if (report[0] & 0x10)` (exemplo Play/Pause).

### 3.4 Comportamentos por botão (crítico para a lógica de firmware)
1. **Todos** enviam pulso limpo `<valor>` → `00 00`. O release chega na hora,
   **mesmo com o botão fisicamente pressionado**.
2. **Consequência decisiva:** o controle **não expõe duração de aperto**. Logo,
   **HOLD por medição de tempo é IMPOSSÍVEL** neste controle. (Descartado.)
3. **Vol+ / Vol−:** ao segurar, o firmware do controle faz **auto-repeat** —
   envia `<valor>`/`00 00` repetidamente (~4 Hz). Pode ser usado como "repeat"
   se desejado, mas conflita com detecção de duplo-toque (janela curta).
4. **← (retroceder):** segurar 1-2s abre o navegador no host, mas **nada
   aparece no report** — esse "hold" é interceptado pelo firmware do controle e
   sai por outro canal (report ID diferente ou AExx) invisível ao WebHID.
   **Não usável** por este projeto. Ignorar; usar só o tap do ←.

### 3.5 Expansão de ações — decisão
- **HOLD:** descartado (hardware não expõe duração).
- **DUPLO-TOQUE:** adotado. 100% lógica no firmware (conta toques do mesmo
  botão dentro de uma janela). Não depende do controle. Robusto.
- **MULTI-PRESS (combinações):** possível em teoria (bitmap permite 2 bits),
  **não testado**. Só investigar se precisar de mais slots.

---

## 4. Arquitetura

```
┌──────────────┐   BLE (central)      ┌──────────────┐   USB HID (device)   ┌──────────┐
│  DQX-Q7      │ ───notify report───► │  ESP32-S3    │ ───keyboard report──►│   PC     │
│  (5 botões)  │  Consumer 0x0C 2B    │  (a ponte)   │  setas/enter/esc     │  Zwift   │
└──────────────┘                      └──────────────┘                      └──────────┘
        │                                    │
        │                              lê config JSON
        │                              (LittleFS)
        └── HID 0x1812 padrão                │
            (visível no Windows,             └── GPIO jumper → modo config web (Fase 2)
             oculto no iOS)
```

- **Entrada:** ESP32-S3 como **BLE central**, assina as notificações HID do
  controle, lê o report de 2 bytes, aplica máscara de bits.
- **Processamento:** consulta o mapa (JSON no LittleFS), decide a tecla,
  aplica lógica de single/double-tap.
- **Saída:** **USB HID keyboard** (TinyUSB) — o PC vê um teclado comum plugado.
  Saída por cabo USB = 100% suportada pelo Zwift PC (evita limitações de
  teclado BLE no host).

### Toolchain (confirmada)
- **Arduino-ESP32 (core 3.x)**
- **TinyUSB** (lado USB HID keyboard)
- **NimBLE-Arduino** (lado BLE central) — leve, coexiste bem com TinyUSB
- Armazenamento: **LittleFS** (JSON de config), **NVS** (chaves de bond BLE)

> Nota: sem a amarra do ESP-IDF 4.4 que existe no projeto ZX-Wespi. Aqui usa-se
> o stack novo (Arduino-ESP32 3.x) à vontade — é onde S3 + USB HID + BLE central
> estão melhor suportados.

---

## 5. Mapa de teclas (config inicial)

Filosofia: **navegação estilo Apple Remote**. 4 direcionais instantâneos +
Enter (single) / Esc (double) no Play/Pause.

| Botão        | Máscara | Single (instantâneo) | Double            |
|--------------|:-------:|----------------------|-------------------|
| ← retroceder | `0x08`  | `LEFT`               | — (livre)         |
| → avançar    | `0x04`  | `RIGHT`              | — (livre)         |
| Vol+         | `0x01`  | `UP`                 | — (livre)         |
| Vol−         | `0x02`  | `DOWN`               | — (livre)         |
| Play/Pause   | `0x10`  | `RETURN` (Enter)     | `ESCAPE` (voltar) |

- **4 direcionais = instantâneos** (sem duplo-toque → sem latência de janela →
  navegação fluida como o Apple Remote).
- **Só Play/Pause tem duplo-toque** → só ele paga a latência da janela (~300ms),
  o que é imperceptível para Enter/Esc em menu.
- Duplos-toques dos demais botões ficam livres para expansão futura via JSON.

### Formato do arquivo de config (`/config.json` no LittleFS)

```json
{
  "double_tap_window_ms": 300,
  "buttons": {
    "back":       { "mask": "0x08", "single": "LEFT" },
    "forward":    { "mask": "0x04", "single": "RIGHT" },
    "vol_up":     { "mask": "0x01", "single": "UP" },
    "vol_down":   { "mask": "0x02", "single": "DOWN" },
    "play_pause": { "mask": "0x10", "single": "RETURN", "double": "ESCAPE" }
  }
}
```

Regras do parser:
- Botão **sem** campo `"double"` → modo **instantâneo** (dispara `single` no ato
  do press, não espera janela).
- Botão **com** `"double"` → aguarda `double_tap_window_ms`; 1 toque = `single`,
  2 toques = `double`.
- Nomes de tecla → mapear para keycodes HID (tabela USB HID Usage Page 0x07).
  Suportar ao menos: `LEFT RIGHT UP DOWN RETURN ESCAPE SPACE TAB`,
  letras `A–Z`, dígitos `0–9`, `F1–F12`, `PAGEUP PAGEDOWN`.

---

## 6. Roadmap de implementação

### Fase 1 — Núcleo funcional (SEM Wi-Fi)
Objetivo: provar BLE central + USB HID + remapeamento, config por arquivo.

- [ ] Bring-up: inicializar TinyUSB (HID keyboard) **e** NimBLE central juntos,
      na ordem correta (ponto de atenção — ver §7).
- [ ] USB HID keyboard: descriptor de teclado padrão; função `sendKey(keycode)`
      com press+release.
- [ ] BLE central: scan → conectar ao DQX-Q7 (`98:4a:c0:ce:bf:a2`).
- [ ] Bond/pairing: parear e **persistir chave no NVS** para reconectar sem
      reparear.
- [ ] Descobrir serviço HID / característica de report; **assinar notificações**.
      (Se o HID `0x1812` não for acessível como central, investigar leitura via
      boot protocol ou o report que o WebHID viu — Consumer 0x0C, 2 bytes.)
- [ ] Parser do report: ler 2 bytes, aplicar máscaras `0x01`–`0x10`, detectar
      transição "pressionado" (bit sobe) e "solto" (`00 00`).
- [ ] Config: LittleFS + parser JSON (`ArduinoJson`) do `/config.json`.
- [ ] Máquina de estados por botão:
      - modo **instant** → dispara no press.
      - modo **double** → temporizador de janela; 1 vs 2 toques.
      - filtrar **auto-repeat** do volume (rajada ~4Hz não deve virar N toques).
- [ ] Mapa nome→keycode HID.
- [ ] Teste ponta-a-ponta no Zwift PC (navegar menu).

### Fase 2 — Config via web (sob demanda por GPIO)
Objetivo: editar o JSON sem recompilar, sem sacrificar o rádio no uso normal.

- [ ] Ler **GPIO (jumper/botão)** no boot:
      - inativo → modo normal, **Wi-Fi desligado** (rádio 100% p/ BLE).
      - ativo → sobe **AP** com página web de edição do `/config.json`.
- [ ] Página web: form que lê/escreve o JSON no LittleFS; salvar → reboot em
      modo normal.
- [ ] (Opcional) validação do JSON no submit.

> O JSON é o **contrato** entre as duas fases. A Fase 2 só escreve nesse
> arquivo; o núcleo da Fase 1 não muda. Desenhar o loader da Fase 1 já pronto
> para reler o arquivo.

### Futuro / opcional
- [ ] Expansão de ações via duplos-toques livres (editar JSON).
- [ ] Testar multi-press (2 bits simultâneos) se precisar de mais slots.
- [ ] Indicador de status (LED): conectado / desconectado / modo config.
- [ ] Deep sleep + wake por atividade BLE (se um dia for a bateria).

---

## 7. Pontos de atenção / riscos conhecidos

1. **Coexistência BLE central + USB HID no S3.**
   É o ponto que mais exige cuidado. TinyUSB no modo USB HID + NimBLE central
   rodando juntos: atenção à ordem de inicialização dos stacks. Território
   mapeado, mas não é "blink de primeira". Isolar o bring-up.

2. **Acesso ao HID como central.**
   No iOS o HID `0x1812` é oculto, mas isso é política do iOS — como **central**,
   o S3 deveria conseguir falar com o HID padrão do controle (que o Windows
   enxerga). Se o `0x1812` não vier no discovery, alternativas: boot protocol
   host, ou verificar se o report Consumer (o mesmo do WebHID) vem por uma
   característica acessível. **Validar cedo.**

3. **Reconexão após sleep do controle (risco de UX).**
   O DQX-Q7 dorme para poupar bateria e só volta a anunciar quando uma tecla é
   apertada. O S3 (central) precisa detectar o re-advertising e reconectar
   rápido, mantendo o bond. **Caso ruim: o primeiro toque após o sleep se
   perde** na reconexão ("apertar 1x para acordar, depois usar"). É
   comportamento do controle; minimizar com reconexão agressiva + bond salvo.

4. **HOLD não existe.** Já descartado — não tentar reintroduzir. A duração de
   aperto não é observável neste hardware.

5. **Auto-repeat do volume vs duplo-toque.** Se algum dia habilitar double nos
   botões de volume, usar janela curta ou a rajada vira falso-duplo.

6. **Latência do duplo-toque.** Inerente: o toque único de um botão com double
   configurado só dispara após a janela. Por isso os direcionais são instantâneos
   (sem double) e só o Play/Pause paga a latência.

---

## 8. Referência rápida (cheat sheet)

- **Controle:** DQX-Q7, BLE, `98:4a:c0:ce:bf:a2`, JieLi, spoofa Apple VID.
- **Report:** Consumer (0x0C), Report ID 1, 2 bytes, bitmap.
  `Vol+=0x01 Vol-=0x02 →=0x04 ←=0x08 Play=0x10`, release=`00 00`.
- **Placa:** ESP32-S3 N16R8. **Toolchain:** Arduino-ESP32 3.x + TinyUSB + NimBLE.
- **Saída:** USB HID keyboard → Zwift PC.
- **Config:** `/config.json` no LittleFS. Fase 2: web via AP sob demanda (GPIO).
- **Mapa:** ←→↑↓ nas 4 direções + Enter (single Play) / Esc (double Play).
- **Sem HOLD.** Duplo-toque para expandir. Companion segue para ações in-game.

---

## 9. Libraries sugeridas (a validar versões no VS Code / PlatformIO)

- `Adafruit TinyUSB Library` (ou o TinyUSB do core Arduino-ESP32 3.x)
- `NimBLE-Arduino` (h2zero)
- `ArduinoJson` (v7)
- `LittleFS` (embutido no core ESP32)

> Recomendado usar **PlatformIO** no VS Code (gerência de deps e board S3 mais
> previsível que o Arduino IDE). Board: `esp32-s3-devkitc-1` (ajustar flash 16MB
> / PSRAM 8MB conforme o N16R8). Habilitar USB CDC/HID no `platformio.ini`
> (`build_flags` do TinyUSB e `board_build.arduino.usb_mode`).

---

## 10. Portas USB do DevKit S3 (bancada / PlatformIO)

O DevKit S3 tem **duas** USB-C, com papéis diferentes e **não** intercambiáveis:

| Porta física | Chip / caminho | Papel | Nesta máquina |
|--------------|----------------|-------|---------------|
| **UART** | Conversor CH340/CP2102 → S3 | Gravar firmware + monitor serial (`Serial.print`) | **COM4** |
| **USB nativa (OTG)** | GPIO19/20 direto no S3 | O "teclado" HID visto pelo PC | **COM5** (ver ressalva) |

### Assinatura no Device Manager
- **UART (COM4):** aparece em *Portas (COM e LPT)* como *"Silicon Labs CP210x"*
  ou *"USB-SERIAL CH340"*. VID do conversor (`10C4` ou `1A86`). **Estável** —
  aparece sempre, independente do firmware.
- **USB nativa (COM5):** é **camaleão**, muda conforme o firmware:
  - Firmware de fábrica / CDC ligado → *Portas (COM)* como *"USB Serial Device"*
    ou *"JTAG/serial debug unit"*, **VID `303A`** (Espressif) = assinatura do S3.
  - **Firmware HID do projeto rodando → SAI de "Portas COM"** e aparece em
    *Teclados* + *Dispositivos de interface humana (HID)* como *"HID Keyboard
    Device"*. **Neste modo a COM5 deixa de existir como COM.**
  - Modo bootloader (BOOT+RESET) → USB genérico `303A PID_0002`.

> ⚠️ **Regra prática:** o número de porta que importa para o build é a
> **COM4 (UART)** — por ela se grava e loga, e ela é estável. A **COM5 só é uma
> COM enquanto o USB nativo estiver em modo CDC**; assim que o firmware HID subir,
> ela vira teclado e some das COMs. **Não** usar COM5 como `upload_port`.

### Fluxo de gravação recomendado
- **Gravar + logar:** sempre pela **COM4 (UART)**. Deixa a USB nativa livre para o HID.
- **Testar o teclado:** plugar a **USB nativa** no PC → ela enumera como teclado.
- Dá para manter **as duas plugadas** ao mesmo tempo em dev: grava/loga na COM4,
  testa o HID na USB nativa, sem trocar cabo.
- Se algum dia gravar pela USB nativa e o PC não achar a porta: **segurar BOOT,
  apertar RESET, soltar BOOT** (modo download). O firmware HID "sequestra" o USB
  nativo, por isso o bootloader manual.

### `platformio.ini` — pontos de partida
```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino

; --- Gravação e monitor pela UART (COM4) ---
upload_port = COM4
monitor_port = COM4
monitor_speed = 115200

; --- USB nativo como HID (TinyUSB), NÃO como CDC ---
; O S3 tem USB-OTG nativo; para o teclado HID, usar o modo TinyUSB.
board_build.arduino.usb_mode = 1              ; 1 = TinyUSB (permite HID)
board_build.arduino.usb_cdc_on_boot = 0       ; 0 = não ocupar o USB nativo com CDC serial
                                              ; (log fica na UART/COM4, USB nativa livre p/ HID)

; --- Flash 16MB / PSRAM 8MB (N16R8) ---
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=0

lib_deps =
    h2zero/NimBLE-Arduino
    bblanchon/ArduinoJson@^7
    ; TinyUSB: usar o embutido no core Arduino-ESP32 3.x (via ARDUINO_USB_MODE)
    ; ou adafruit/Adafruit TinyUSB Library se optar por ela
```

> Notas:
> - `usb_cdc_on_boot = 0` é o que mantém a **COM5 fora do caminho** — sem CDC no
>   USB nativo, ele fica dedicado ao HID e o log vai todo pela COM4. Se preferir
>   ter também um CDC serial pelo USB nativo durante o debug, pôr `= 1`, mas aí
>   convivem CDC + HID no mesmo USB e a enumeração fica mais confusa.
> - Confirmar o nome exato do partition `default_16MB.csv` disponível no core
>   instalado (pode variar). Ajustar se o build reclamar.
