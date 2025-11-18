// main.c
#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_gpdma.h"

#include "max_controll.h"
#include "battleship_max.h"

// ================= Macros base =================

#define BIT(x)          (1u << (x))

// Ratio del ADC
#define ADC_RATE        180000u

// Límites de movimiento del joystick
#define UPPER_LIM       0xE00u
#define LOWER_LIM       0x1FFu

// Joystick HW504:
// VRx -> ADC_CHANNEL_0
// VRy -> ADC_CHANNEL_1
// SW  -> P2.10 (entrada con pull-up)
#define JOY_BTN_PORT    2u
#define JOY_BTN_PIN     10u

// Botón extra para rotación de barco -> P2.11
#define ROT_BTN_PORT    2u
#define ROT_BTN_PIN     11u

// Botón extra para PAUSA -> P2.12
#define PAUSE_BTN_PORT  2u
#define PAUSE_BTN_PIN   12u

// LED en P0.22 (solo por si querés debug visual)
#define PIN_22          ((uint32_t)(1u << 22))

// ====== Códigos UART de estado de juego ======
#define UART_INICIO_JUEGO  0x00
#define UART_PAUSA         0x01
#define UART_JUGADOR1      0x02
#define UART_JUGADOR2      0x03   // reservado para futuro 2 jugadores
#define UART_GANADOR_J1    0x04
#define UART_GANADOR_J2    0x05

// Variable que envía el DMA constantemente por UART0
volatile uint8_t data = UART_INICIO_JUEGO;

// Descriptor de LLI para DMA UART0 TX
static GPDMA_LLI_Type cfg_UART0_LLI_CH7;
#include "LPC17xx.h"
#include "max_controll.h"

// CS en P0.16
#define MAX_CS_PORT 0
#define MAX_CS_PIN  16

// ========= Utils internos =========
static uint8_t reverseBits(uint8_t v){
    v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
    return v;
}

static void MAX_WriteRegToAll(uint8_t targetDev, uint8_t reg, uint8_t data){
    int i;

    if (targetDev >= MAX_NUM_DEVICES) return;

    MAX_HAL_Select();

    // Mandamos de dev3 a dev0 (el primero en la cadena es el #0)
    for (i = MAX_NUM_DEVICES - 1; i >= 0; i--) {
        uint8_t r = (i == (int)targetDev) ? reg  : MAX_REG_NOOP;
        uint8_t d = (i == (int)targetDev) ? data : 0x00u;
        MAX_HAL_SendByte(r);
        MAX_HAL_SendByte(d);
    }

    MAX_HAL_Deselect();
}

// ========= HAL para LPC1769 =========

void MAX_HAL_Select(void){
    LPC_GPIO0->FIOCLR = (1U << MAX_CS_PIN);
}
void MAX_HAL_Deselect(void){
    LPC_GPIO0->FIOSET = (1U << MAX_CS_PIN);
}

void MAX_HAL_SendByte(uint8_t data){
    LPC_SSP0->DR = data;
    // Esperar mientras SSP0 está ocupado (bit BSY = SR[4])
    while (LPC_SSP0->SR & (1U << 4)) {
        // loop vacío
    }
}

void MAX_SPI0_Init(void){
    // Habilitar clock SSP0
    LPC_SC->PCONP |= (1U << 21);  // PCSSP0

    // P0.15 = SCK0, P0.17 = MISO0, P0.18 = MOSI0
    // Configurar PINSEL para esas funciones
    LPC_PINCON->PINSEL0 &= ~(3U << 30);              // P0.15
    LPC_PINCON->PINSEL1 &= ~((3U << 2) | (3U << 4)); // P0.17, P0.18

    LPC_PINCON->PINSEL0 |=  (2U << 30);              // P0.15 -> SCK0
    LPC_PINCON->PINSEL1 |=  (2U << 2) | (2U << 4);   // P0.17->MISO0, P0.18->MOSI0

    // CS como GPIO salida
    LPC_GPIO0->FIODIR |= (1U << MAX_CS_PIN);
    MAX_HAL_Deselect();

    // Config SSP0: 8 bits, modo 0, master
    LPC_SSP0->CR0 = 0x0707;    // 8 bits, CPOL=0, CPHA=0, SCR=7 (ajustable)
    LPC_SSP0->CR1 = (1U << 1); // SSE = 1 (habilita SSP)
    LPC_SSP0->CPSR = 2;        // prescaler (ajustar si querés otra velocidad)
}

// ========= API pública =========

void MAX_InitDevice(uint8_t dev){
    if (dev >= MAX_NUM_DEVICES) return;

    MAX_WriteRegToAll(dev, MAX_REG_DISPLAYTEST, 0x00u);
    MAX_WriteRegToAll(dev, MAX_REG_SCAN_LIMIT,  0x07u);
    MAX_WriteRegToAll(dev, MAX_REG_DECODE_MODE, 0x00u);
    MAX_WriteRegToAll(dev, MAX_REG_SHUTDOWN,    0x01u);
    MAX_WriteRegToAll(dev, MAX_REG_INTENSITY,   0x04u);

    MAX_Clear(dev);
}

void MAX_InitAll(void){
    uint8_t d;
    for (d = 0; d < MAX_NUM_DEVICES; d++) {
        MAX_InitDevice(d);
    }
}

void MAX_Clear(uint8_t dev){
    uint8_t r;
    if (dev >= MAX_NUM_DEVICES) return;
    for (r = 0; r < 8; r++) {
        MAX_SetRow(dev, r, 0x00u);
    }
}

void MAX_SetRow(uint8_t dev, uint8_t row, uint8_t val){
    if (dev >= MAX_NUM_DEVICES || row > 7) return;
    MAX_WriteRegToAll(dev, (uint8_t)(MAX_REG_DIGIT0 + row), reverseBits(val));
}

void MAX_DrawRows(uint8_t dev, const uint8_t rows[8]){
    uint8_t r;
    if (dev >= MAX_NUM_DEVICES) return;
    for (r = 0; r < 8; r++) {
        MAX_SetRow(dev, r, rows[r]);
    }
}

// ================ Base de tiempo (HAL para la librería) =================

static volatile uint32_t g_msTicks = 0u;
static uint32_t lcg_state = 1234567u;

// Prototipos HAL que usa la librería
uint32_t BS_Hal_GetMillis(void);
uint32_t BS_Hal_GetRandom(void);

// SysTick a 1 ms REAL
static void configSysTick(void) {
    // Asegurarse de que SystemCoreClock tiene el valor correcto
    SystemCoreClockUpdate();

    uint32_t reload = SystemCoreClock / 1000u;  // 1 ms

    SysTick->LOAD = reload - 1u;
    SysTick->VAL  = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
}

// SysTick: incrementa ms y deja que la librería maneje blinks, etc.
void SysTick_Handler(void) {
    g_msTicks++;
    BS_AnimationsUpdate(g_msTicks);
}

uint32_t BS_Hal_GetMillis(void) {
    return g_msTicks;
}

uint32_t BS_Hal_GetRandom(void) {
    // LCG simple
    lcg_state = 1664525u * lcg_state + 1013904223u;
    return lcg_state;
}

// ================ Joystick / ADC ================

static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

// Flag: todos los barcos (2,4,6) ya colocados
static volatile uint8_t g_allShipsPlaced = 0u;

// Prototipos
static void cfgGPIO(void);
static void GPIO_Port2_UnusedAsOutput(void);
static void cfgTimerForADC(void);
static void cfgADC(void);
static void cfgUART0(void);
static void cfgDMA_UART0_TX(void);

// ================ main ================

int main(void) {
    SystemInit();

    // SysTick para base de tiempo de la librería (blink, etc.)
    configSysTick();

    // Inicializar lógica completa Battleship + MAX internamente
    BS_GameInit();

    // Estado inicial UART: inicio de juego (previo a colocar barcos)
    data = UART_INICIO_JUEGO;

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgTimerForADC();
    cfgADC();

    // UART + DMA transmitiendo siempre el byte "data"
    cfgUART0();
    cfgDMA_UART0_TX();

    while (1) {
        // Todo se maneja por interrupciones:
        //  - SysTick_Handler -> BS_AnimationsUpdate()
        //  - ADC_IRQHandler -> joystick analógico
        //  - EINT3_IRQHandler -> botones (colocar / rotar / disparar / pausa)
        __WFI();   // opcional: dormir hasta próxima IRQ
    }
}

// ================ GPIO (joystick + rotación + pausa + UART TX + LED debug) ================

static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // --- Botón del joystick en P2.10 como GPIO entrada con pull-up ---
    cfgPin.portNum   = PINSEL_PORT_2;
    cfgPin.pinNum    = PINSEL_PIN_10;
    cfgPin.funcNum   = PINSEL_FUNC_0;      // GPIO
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);
    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0); // 0 = input

    // --- Botón de rotación en P2.11 como GPIO entrada con pull-up ---
    cfgPin.pinNum    = PINSEL_PIN_11;
    cfgPin.funcNum   = PINSEL_FUNC_0;
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);
    GPIO_SetDir(ROT_BTN_PORT, BIT(ROT_BTN_PIN), 0); // 0 = input

    // --- Botón de PAUSA en P2.12 como GPIO entrada con pull-up ---
    cfgPin.pinNum    = PINSEL_PIN_12;
    cfgPin.funcNum   = PINSEL_FUNC_0;
    cfgPin.pinMode   = PINSEL_PULLUP;
    cfgPin.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPin);
    GPIO_SetDir(PAUSE_BTN_PORT, BIT(PAUSE_BTN_PIN), 0); // 0 = input

    // --- TXD0 en P0.2 ---
    PINSEL_CFG_Type cfgPinTXD0;
    cfgPinTXD0.portNum   = PINSEL_PORT_0;
    cfgPinTXD0.pinNum    = PINSEL_PIN_2;
    cfgPinTXD0.funcNum   = PINSEL_FUNC_1;     // TXD0
    cfgPinTXD0.pinMode   = PINSEL_TRISTATE;
    cfgPinTXD0.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPinTXD0);

    // --- LED en P0.22 (opcional debug) ---
    PINSEL_CFG_Type cfgPinLed;
    cfgPinLed.portNum   = PINSEL_PORT_0;
    cfgPinLed.pinNum    = PINSEL_PIN_22;
    cfgPinLed.funcNum   = PINSEL_FUNC_0;      // GPIO
    cfgPinLed.pinMode   = PINSEL_TRISTATE;
    cfgPinLed.openDrain = PINSEL_OD_NORMAL;
    PINSEL_ConfigPin(&cfgPinLed);
    GPIO_SetDir(0, PIN_22, 1);                // salida

    // --- GPIO interrupt en flanco descendente para P2.10 / P2.11 / P2.12 ---
    // Limpiamos cualquier pendiente previa
    LPC_GPIOINT->IO2IntClr = (1u << JOY_BTN_PIN) |
                              (1u << ROT_BTN_PIN) |
                              (1u << PAUSE_BTN_PIN);
    // Habilitamos interrupción por flanco de bajada
    LPC_GPIOINT->IO2IntEnF |= (1u << JOY_BTN_PIN) |
                              (1u << ROT_BTN_PIN) |
                              (1u << PAUSE_BTN_PIN);

    NVIC_EnableIRQ(EINT3_IRQn);
}

// Poner el resto de los pines de PORT2 como salida para que no floten
static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << JOY_BTN_PIN) |
                           (1u << ROT_BTN_PIN) |
                           (1u << PAUSE_BTN_PIN);
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// ================ Timer0: trigger para ADC =================

static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    // Timer base: prescale en us -> 1 tick = 1000 us = 1 ms
    cfgTimer.prescaleOption = TIM_USVAL;
    cfgTimer.prescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    // Match canal 1 (MAT0.1) para disparar el ADC cada ~125 ms
    cfgMatch01.matchChannel       = TIM_MATCH_1;
    cfgMatch01.intOnMatch         = DISABLE;
    cfgMatch01.resetOnMatch       = ENABLE;
    cfgMatch01.stopOnMatch        = DISABLE;
    cfgMatch01.extMatchOutputType = TIM_TOGGLE;
    cfgMatch01.matchValue         = 124;   // 0..124 -> 125 ms aprox.

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ================ ADC: joystick X/Y =================

static void cfgADC(void) {
    // Inicializar ADC a tu rate
    ADC_Init(ADC_RATE);

    // Pines analógicos para CH0 (VRx) y CH1 (VRy)
    ADC_PinConfig(ADC_CHANNEL_0);
    ADC_PinConfig(ADC_CHANNEL_1);

    // Sin burst, disparo por MAT0.1
    ADC_BurstCmd(DISABLE);
    ADC_StartCmd(ADC_START_ON_MAT01);
    ADC_EdgeStartConfig(ADC_START_ON_FALLING);

    // Empieza leyendo CH0
    ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
    ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);

    // Habilitamos interrupción en CH0 y CH1
    ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
    ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

    NVIC_EnableIRQ(ADC_IRQn);
}

void ADC_IRQHandler(void) {
    // 1) Lectura del joystick analógico (VRx / VRy)
    if (ADC_GlobalGetStatus(ADC_DATA_DONE)) {
        if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE)) {
            // Cambiar a CH1 para la próxima conversión
            ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

            vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

            // Movimiento horizontal con VRx
            BS_Mode mode = BS_GetMode();
            if (vrx > UPPER_LIM) {
                // derecha
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_RIGHT);
                } else { // MODE_SHOT
                    BS_Shot_MoveCursor(BS_DIR_RIGHT);
                }
            } else if (vrx < LOWER_LIM) {
                // izquierda
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_LEFT);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_LEFT);
                }
            }

        } else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE)) {
            // Cambiar de nuevo a CH0
            ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

            vry = ADC_ChannelGetData(ADC_CHANNEL_1);

            // Movimiento vertical con VRy
            BS_Mode mode = BS_GetMode();
            if (vry > UPPER_LIM) {
                // arriba
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_UP);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_UP);
                }
            } else if (vry < LOWER_LIM) {
                // abajo
                if (mode == MODE_PLACE) {
                    BS_Placement_MoveCursor(BS_DIR_DOWN);
                } else {
                    BS_Shot_MoveCursor(BS_DIR_DOWN);
                }
            }
        }
    }

    // battleship_max se encarga internamente de:
    //  - refrescar bloques 0,1,2 en cada acción
    //  - blinks de error, barcos listos, agua, etc. vía BS_AnimationsUpdate()
}

// ================ UART0 + DMA =================

static void cfgUART0(void)
{
    // P0.2 -> TXD0 ; P0.3 -> RXD0
    LPC_PINCON->PINSEL0 |= (1u << 4) | (1u << 6);

    // Power UART0
    LPC_SC->PCONP |= (1u << 3);

    uint32_t PCLK = SystemCoreClock / 4;
    uint32_t DL = PCLK / (16u * 9600u);   // baudrate 9600

    LPC_UART0->LCR = 0x83;                // 8N1 + DLAB
    LPC_UART0->DLL = DL & 0xFFu;
    LPC_UART0->DLM = (DL >> 8);
    LPC_UART0->LCR = 0x03;                // 8N1 sin DLAB

    LPC_UART0->FCR = 0x07;                // FIFO ON + reset
    LPC_UART0->IER = (1u << 7);           // UART0 DMA Mode
}

static void cfgDMA_UART0_TX(void)
{
    GPDMA_Channel_CFG_Type cfg_DMA_CH7;

    NVIC_DisableIRQ(DMA_IRQn);
    GPDMA_Init();

    // LLI que se apunta a sí mismo para enviar siempre "data"
    cfg_UART0_LLI_CH7.srcAddr  = (uint32_t)&data;
    cfg_UART0_LLI_CH7.dstAddr  = (uint32_t)&LPC_UART0->THR;
    cfg_UART0_LLI_CH7.nextLLI  = (uint32_t)&cfg_UART0_LLI_CH7;
    cfg_UART0_LLI_CH7.control  = (1u << 0);  // tamaño = 1 (resto por defecto)

    cfg_DMA_CH7.channelNum     = GPDMA_CHANNEL_7;
    cfg_DMA_CH7.srcConn        = 0;
    cfg_DMA_CH7.dstConn        = GPDMA_UART0_Tx;
    cfg_DMA_CH7.srcMemAddr     = (uint32_t)&data;
    cfg_DMA_CH7.dstMemAddr     = (uint32_t)&LPC_UART0->THR;
    cfg_DMA_CH7.transferType   = GPDMA_M2M;      // dejamos como en tu código
    cfg_DMA_CH7.transferSize   = 1;
    cfg_DMA_CH7.transferWidth  = GPDMA_BYTE;
    cfg_DMA_CH7.linkedList     = (uint32_t)&cfg_UART0_LLI_CH7;

    GPDMA_Setup(&cfg_DMA_CH7);
    GPDMA_ChannelCmd(GPDMA_CHANNEL_7, ENABLE);

    // Disparo inicial por software
    LPC_GPDMA->DMACSoftSReq = (1u << 7);
}

// ================ GPIO IRQ: botones (colocar / rotar / disparar / pausa) ================

void EINT3_IRQHandler(void) {
    uint32_t statusF = LPC_GPIOINT->IO2IntStatF;

    // --- Botón del joystick (P2.10) -> colocar/confirmar/disparar ---
    if (statusF & (1u << JOY_BTN_PIN)) {
        // limpiar primero el flag de interrupción
        LPC_GPIOINT->IO2IntClr = (1u << JOY_BTN_PIN);

        BS_Mode mode = BS_GetMode();

        if (mode == MODE_PLACE) {
            if (!g_allShipsPlaced) {
                // Intentar colocar barco actual (2,4,6)
                BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(BS_Hal_GetMillis());
                if (res == BS_PLACE_ALL_DONE) {
                    // Ya colocamos los 3 barcos
                    g_allShipsPlaced = 1u;
                }
                // Si res == BS_PLACE_INVALID, la lib maneja el blink de error
            } else {
                // Todos los barcos ya colocados, este botón pasa a modo disparo
                BS_EnterShotMode();

                // C) Al entrar en fase de disparos -> jugador1
                data = UART_JUGADOR1;
            }
        } else { // MODE_SHOT
            // En modo disparo: este botón dispara al contrincante
            SHOT_RESULT_t sres = BS_Shot_FireAtCursor();
            (void)sres;
            // La librería se encarga de:
            //  - HIT: mantener led encendido (queda fijo)
            //  - MISS: blink en el agua y luego se apaga (por animación)
            //  - Límite de disparos / ganador J1 se manejan adentro
        }
    }

    // --- Botón de rotación (P2.11) -> solo durante colocación y antes de terminar ---
    if (statusF & (1u << ROT_BTN_PIN)) {
        LPC_GPIOINT->IO2IntClr = (1u << ROT_BTN_PIN);

        if (BS_GetMode() == MODE_PLACE && !g_allShipsPlaced) {
            BS_Placement_RotateCursor();
        }
    }

    // --- Botón de PAUSA (P2.12) ---
    if (statusF & (1u << PAUSE_BTN_PIN)) {
        LPC_GPIOINT->IO2IntClr = (1u << PAUSE_BTN_PIN);

        // B) PAUSA
        data = UART_PAUSA;
    }
}