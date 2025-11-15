#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"

#include "max_controll.h"
#include "battleship_max.h"

// ================= Macros base =================

#define BIT(x)          (1u << (x))

// Ratio del ADC (como en tu código)
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

// ================= SysTick (contador bloque 3) =================

// Tus macros:
#define CORE          100000u
#define TICKS         1000u
#define ST_LOAD       ((TICKS * CORE) - 1u)

// Dígito actual del bloque 3 (counter)
static volatile uint8_t g_counter_digit = 9u;

// Tabla de dígitos (0–9) igual que en battleship_max
static const uint8_t digits[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x3C}, //0
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x7E}, //1
    {0x3C,0x66,0x06,0x0C,0x18,0x60,0x66,0x7E}, //2
    {0x3C,0x66,0x06,0x1C,0x06,0x06,0x66,0x3C}, //3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x1E}, //4
    {0x7E,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C}, //5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x66,0x3C}, //6
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x18}, //7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x3C}, //8
    {0x3C,0x66,0x66,0x3E,0x06,0x06,0x66,0x3C}  //9
};

static void drawDigitBlock3(uint8_t num) {
    uint8_t r;
    if (num > 9u) return;

    // Mismo criterio que la librería: fila lógica 0 abajo -> row 7
    for (r = 0; r < 8u; r++) {
        MAX_SetRow(BS_DEV_COUNTER, (uint8_t)(7u - r), digits[num][r]);
    }
}

static void configSysTick(void) {
    SysTick->LOAD = ST_LOAD;
    SysTick->VAL  = 0u;
    SysTick->CTRL = 0x07u;   // ENABLE + TICKINT + CLKSOURCE
}

// SysTick: cuenta regresiva infinita en bloque 3
void SysTick_Handler(void) {
    if (g_counter_digit == 0u) {
        g_counter_digit = 9u;
    } else {
        g_counter_digit--;
    }
    drawDigitBlock3(g_counter_digit);
}

// ================ Joystick / ADC ================

// Lecturas del ADC
static volatile uint32_t vrx = 0u;
static volatile uint32_t vry = 0u;

// "Tiempo falso" para BS_Placement_TryPlaceCurrentShip()
static volatile uint32_t g_fakeTime = 0u;

// Estado previo del botón del joystick
static volatile uint8_t joyBtnLast = 1u;  // 1 = no presionado (pull-up)

// Prototipos
static void cfgGPIO(void);
static void cfgTimerForADC(void);
static void cfgADC(void);
static void GPIO_Port2_UnusedAsOutput(void);

// ================ main ================

int main(void) {
    SystemInit();

    // SysTick para el contador del bloque 3
    configSysTick();

    // Inicializar SPI0 + MAX + lógica Battleship
    MAX_SPI0_Init();
    BS_GameInit();
    // En este punto:
    //  - Bloque0: pivote/barco en preview
    //  - Bloque1: tablero jugador
    //  - Bloque2: contrincante con barcos 2,4,6
    //  - Bloque3: algún valor inicial (lo vamos a sobrescribir)

    // Forzamos arranque del contador en 9 en el bloque 3
    g_counter_digit = 9u;
    drawDigitBlock3(g_counter_digit);

    cfgGPIO();
    GPIO_Port2_UnusedAsOutput();
    cfgTimerForADC();
    cfgADC();

    while (1) {
        // Todo se maneja por interrupción:
        //  - SysTick: contador en bloque 3
        //  - ADC: joystick + botón + actualización de bloques 0,1,2
        __WFI();   // opcional: dormir hasta próxima IRQ
    }
}

// ================ GPIO (joystick button + limpieza PORT2) ================

static void cfgGPIO(void) {
    PINSEL_CFG_Type cfgPin;

    // Botón del joystick en P2.10 como GPIO entrada con pull-up
    cfgPin.Portnum   = PINSEL_PORT_2;
    cfgPin.Pinnum    = PINSEL_PIN_10;
    cfgPin.Funcnum   = PINSEL_FUNC_0;      // GPIO
    cfgPin.Pinmode   = PINSEL_PINMODE_PULLUP;
    cfgPin.OpenDrain = PINSEL_PINMODE_NORMAL;
    PINSEL_ConfigPin(&cfgPin);

    // Dirección: entrada
    GPIO_SetDir(JOY_BTN_PORT, BIT(JOY_BTN_PIN), 0);
}

// Poner el resto de los pines de PORT2 como salida para que no floten
static void GPIO_Port2_UnusedAsOutput(void) {
    uint32_t mask_inputs = (1u << JOY_BTN_PIN); // solo P2.10 entrada
    LPC_GPIO2->FIODIR |= ~mask_inputs;
}

// ================ Timer0: trigger para ADC =================

static void cfgTimerForADC(void) {
    TIM_TIMERCFG_Type cfgTimer;
    TIM_MATCHCFG_Type cfgMatch01;

    // Timer base: prescale en us -> 1 tick = 1 us * 1000 = 1 ms
    cfgTimer.PrescaleOption = TIM_USVAL;
    cfgTimer.PrescaleValue  = 1000;
    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);

    // Match canal 1 (MAT0.1) para disparar el ADC cada ~125 ms (ajustable)
    cfgMatch01.MatchChannel = TIM_MATCH_1;
    cfgMatch01.IntOnMatch   = DISABLE;
    cfgMatch01.ResetOnMatch = ENABLE;
    cfgMatch01.StopOnMatch  = DISABLE;
    cfgMatch01.ExtMatchOutputType = TIM_EXTMATCH_TOGGLE;
    cfgMatch01.MatchValue   = 124;     // ~125 ms (depende de tu clock real)

    TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);
    TIM_Cmd(LPC_TIM0, ENABLE);
}

// ================ ADC: Joystick X/Y + botón =================

static void cfgADC(void) {
    // Inicializar ADC a tu rate
    ADC_Init(ADC_RATE);

    // Pines analógicos para CH0 y CH1
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
    // "tiempo" artificial para pasar a BS_Placement_TryPlaceCurrentShip()
    g_fakeTime += 10u;

    if (ADC_GlobalGetStatus(ADC_DATA_DONE)) {
        // Secuencia CH0 -> CH1 alternando
        if (ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE)) {

            // Cambiar a CH1 para la próxima
            ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

            vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

            // Movimiento horizontal con VRx
            if (vrx > UPPER_LIM) {
                BS_Placement_MoveCursor(BS_DIR_RIGHT);
            } else if (vrx < LOWER_LIM) {
                BS_Placement_MoveCursor(BS_DIR_LEFT);
            }

        } else if (ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE)) {
            // Cambiar de nuevo a CH0
            ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
            ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

            vry = ADC_ChannelGetData(ADC_CHANNEL_1);

            // Movimiento vertical con VRy
            if (vry > UPPER_LIM) {
                BS_Placement_MoveCursor(BS_DIR_UP);
            } else if (vry < LOWER_LIM) {
                BS_Placement_MoveCursor(BS_DIR_DOWN);
            }
        }
    }

    // Lectura del botón del joystick (SW en P2.10)
    {
        uint8_t btnNow = (GPIO_ReadValue(JOY_BTN_PORT) & BIT(JOY_BTN_PIN)) ? 1u : 0u;

        // Flanco de bajada: 1 -> 0 (presiona)
        if (btnNow == 0u && joyBtnLast == 1u) {
            BS_PlaceResult res = BS_Placement_TryPlaceCurrentShip(g_fakeTime);
            (void)res; // por ahora no usamos res, pero podrías chequear BS_PLACE_ALL_DONE
        }

        joyBtnLast = btnNow;
    }

    // battleship_max se encarga de refrescar bloques 0,1,2 internamente
}
