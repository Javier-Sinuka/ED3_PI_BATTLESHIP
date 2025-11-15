// battleship_input.c (por ejemplo)
#include "library_bs/battleship_max.h"
#include <stdint.h>
#include <stdbool.h>

/* ========= CONFIG JOYSTICK ========= */

#define JOY_ADC_CH_X   0   // AD0.0
#define JOY_ADC_CH_Y   1   // AD0.1

#define JOY_LEFT_MIN    0
#define JOY_LEFT_MAX    0
#define JOY_RIGHT_MIN   0
#define JOY_RIGHT_MAX   0

#define JOY_UP_MIN      0
#define JOY_UP_MAX      0
#define JOY_DOWN_MIN    0
#define JOY_DOWN_MAX    0

/* ========= PROTOTIPOS QUE VOS IMPLEMENTÁS PARA BOTONES ========= */
/*  Estos NO forman parte del núcleo de la librería Battleship,
 *  pero son el “gancho” para la app de LPC.
 */

// Inicializa los pines de los 4 pulsadores (dirección, pull-ups, etc).
void BS_ActionButtonsInit(void);

// Devuelve el estado de los 4 botones de acción.
// true  = NO presionado (nivel alto)
// false = presionado  (nivel bajo)
void BS_GetActionButtons(bool *place, bool *show,
                         bool *rotate, bool *confirm);

/* ========= FUNCIONES AUXILIARES DEL ADC (ejemplo) ========= */
static uint16_t ADC_ReadChannel(uint8_t ch)
{
    // TODO: rellenar con tu código de ADC real.
    // Esto es solo un stub:
    (void)ch;
    return 2048;
}

/* ========= ESTA ES LA FUNCIÓN QUE USA LA LIB DE BATTLESHIP ========= */

void BS_Hal_GetKeyLevels(bool levels[K_COUNT])
{
    // 1) Por defecto, todo HIGH (no presionado)
    for (int i = 0; i < K_COUNT; i++) {
        levels[i] = true;
    }

    // 2) Leer joystick
    uint16_t x = ADC_ReadChannel(JOY_ADC_CH_X);
    uint16_t y = ADC_ReadChannel(JOY_ADC_CH_Y);

    // --- Direcciones a partir del joystick ---
    // LOW (false) = “tecla presionada” para la librería

    // Izquierda
    if (x >= JOY_LEFT_MIN && x <= JOY_LEFT_MAX) {
        levels[K_LEFT] = false;
    }
    // Derecha
    if (x >= JOY_RIGHT_MIN && x <= JOY_RIGHT_MAX) {
        levels[K_RIGHT] = false;
    }
    // Arriba
    if (y >= JOY_UP_MIN && y <= JOY_UP_MAX) {
        levels[K_UP] = false;
    }
    // Abajo
    if (y >= JOY_DOWN_MIN && y <= JOY_DOWN_MAX) {
        levels[K_DOWN] = false;
    }

    // 3) Leer botones de acción mediante función de usuario
    bool b_place, b_show, b_rotate, b_confirm;
    BS_GetActionButtons(&b_place, &b_show, &b_rotate, &b_confirm);

    if (!b_place)   levels[K_PLACE]   = false;
    if (!b_show)    levels[K_SHOW]    = false;
    if (!b_rotate)  levels[K_ROTATE]  = false;
    if (!b_confirm) levels[K_CONFIRM] = false;
}
