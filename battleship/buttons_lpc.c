// buttons_lpc.c (archivo tuyo de la app)
#include "../CMSISv2p00_LPC17xx/inc/LPC17xx.h"
#include <stdbool.h>
#include "library_bs/battleship_max.h"

// Pines de ejemplo:
#define BTN_PORT   2
#define BTN_PLACE_PIN   10
#define BTN_SHOW_PIN    11
#define BTN_ROTATE_PIN  12
#define BTN_CONFIRM_PIN 13

static inline bool readGPIO(uint8_t port, uint8_t pin)
{
    if (port == 0) return (LPC_GPIO0->FIOPIN & (1U << pin)) != 0;
    if (port == 1) return (LPC_GPIO1->FIOPIN & (1U << pin)) != 0;
    if (port == 2) return (LPC_GPIO2->FIOPIN & (1U << pin)) != 0;
    if (port == 3) return (LPC_GPIO3->FIOPIN & (1U << pin)) != 0;
    return true;
}

// 1) LLAMAR A ESTO EN main() ANTES DE BS_GameInit()
void BS_ActionButtonsInit(void)
{
    // P2.10..2.13 como entrada
    LPC_GPIO2->FIODIR &= ~((1U << BTN_PLACE_PIN) |
                           (1U << BTN_SHOW_PIN)  |
                           (1U << BTN_ROTATE_PIN)|
                           (1U << BTN_CONFIRM_PIN));

    // Pull-up internos (opcional, si no usás resistencias externas)
    LPC_PINCON->PINMODE4 &= ~((3U << 20) | (3U << 22) |
                              (3U << 24) | (3U << 26));
    // 00 = pull-up
}

// 2) LLAMADA DESDE BS_Hal_GetKeyLevels()
void BS_GetActionButtons(bool *place, bool *show,
                         bool *rotate, bool *confirm)
{
    // Botones con pull-up: LOW = presionado
    *place   =  readGPIO(BTN_PORT, BTN_PLACE_PIN);
    *show    =  readGPIO(BTN_PORT, BTN_SHOW_PIN);
    *rotate  =  readGPIO(BTN_PORT, BTN_ROTATE_PIN);
    *confirm =  readGPIO(BTN_PORT, BTN_CONFIRM_PIN);
}
