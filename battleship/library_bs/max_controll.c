#include "max_controll.h"

//================== Utils internos ======================

/**
 * Invierte el orden de bits de un byte (bit0 <-> bit7).
 * Útil para adaptar orientación física de las columnas.
 */
static uint8_t reverseBits(uint8_t v){
    v = (v & 0xF0u) >> 4 | (v & 0x0Fu) << 4;
    v = (v & 0xCCu) >> 2 | (v & 0x33u) << 2;
    v = (v & 0xAAu) >> 1 | (v & 0x55u) << 1;
    return v;
}

/**
 * Escribe (reg,data) al dispositivo lógico `targetDev`, enviando
 * NOOP al resto de los MAX en cascada.
 */
static void MAX_WriteRegToAll(uint8_t targetDev, uint8_t reg, uint8_t data){
    if(targetDev >= MAX_NUM_DEVICES) return;

    MAX_HAL_Select();

    // Enviamos de dev (MAX_NUM_DEVICES-1) a 0, para que el 0
    // sea el más cercano al micro, por ejemplo.
    for(int i = MAX_NUM_DEVICES - 1; i >= 0; i--){
        uint8_t r = (i == (int)targetDev) ? reg  : MAX_REG_NOOP;
        uint8_t d = (i == (int)targetDev) ? data : 0x00u;
        MAX_HAL_SendByte(r);
        MAX_HAL_SendByte(d);
    }

    MAX_HAL_Deselect();
}

//================== Metodos Publicos ==========================

void MAX_InitDevice(uint8_t dev){
    if(dev >= MAX_NUM_DEVICES) return;

    MAX_WriteRegToAll(dev, MAX_REG_DISPLAYTEST, 0x00u);
    MAX_WriteRegToAll(dev, MAX_REG_SCAN_LIMIT,  0x07u);
    MAX_WriteRegToAll(dev, MAX_REG_DECODE_MODE, 0x00u);
    MAX_WriteRegToAll(dev, MAX_REG_SHUTDOWN,    0x01u); // display ON
    MAX_WriteRegToAll(dev, MAX_REG_INTENSITY,   0x04u); // brillo medio

    MAX_Clear(dev);
}

void MAX_InitAll(void){
    for(uint8_t d = 0; d < MAX_NUM_DEVICES; d++){
        MAX_InitDevice(d);
    }
}

void MAX_Clear(uint8_t dev){
    if(dev >= MAX_NUM_DEVICES) return;
    for(uint8_t r = 0; r < 8; r++){
        MAX_SetRow(dev, r, 0x00u);
    }
}

void MAX_SetRow(uint8_t dev, uint8_t row, uint8_t val){
    if(dev >= MAX_NUM_DEVICES || row > 7) return;
    // DIGIT0 + row
    MAX_WriteRegToAll(dev, (uint8_t)(MAX_REG_DIGIT0 + row), reverseBits(val));
}

void MAX_DrawRows(uint8_t dev, const uint8_t rows[8]){
    if(dev >= MAX_NUM_DEVICES) return;
    for(uint8_t r = 0; r < 8; r++){
        MAX_SetRow(dev, r, rows[r]);
    }
}
