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
    uint32_t timeout;
    volatile uint32_t dummy;

    // 1) Esperar a que el FIFO de transmisión NO esté lleno (TNF = SR[1] = 1)
    timeout = 1000000u;
    while ( (LPC_SSP0->SR & (1U << 1)) == 0U ) {   // TNF == 0 -> FIFO lleno
        if (--timeout == 0U) {
            // Timeout de seguridad: salimos para no clavarnos
            return;
        }
    }

    // 2) Escribir el dato
    LPC_SSP0->DR = data;

    // 3) Esperar a que termine la transferencia (BSY = SR[4] -> 0)
    timeout = 1000000u;
    while ( (LPC_SSP0->SR & (1U << 4)) != 0U ) {   // BSY == 1 -> ocupado
        if (--timeout == 0U) {
            // Timeout de seguridad
            break;
        }
    }

    // 4) Limpiar FIFO de recepción (RNE = SR[2]) leyendo DR
    while ( (LPC_SSP0->SR & (1U << 2)) != 0U ) {   // RNE == 1 -> hay dato
        dummy = LPC_SSP0->DR;
        (void)dummy;
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
