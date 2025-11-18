#include "LPC17xx.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_gpdma.h"
#include "lpc17xx_exti.h"

// Desplazamiento de bit.
#define BIT(x) (1<<x)
// Movimiento de los leds.
#define RED_LED BIT(22)
#define GREEN_LED BIT(25)
#define BLUE_LED BIT(26)
// Ratio del ADC.
#define ADC_RATE 180000
// Lugar de memoria donde se guardan las converciones
#define ADC_MEM 0x20080000
// Valores tentativos para guardar datos.
#define SOURCE_MEM 0x200800F0
#define DEST_MEM 0x20080F00
// Limites para definir un movimiento del analogico
#define UPPER_LIM 0xE00
#define LOWER_LIM 0x1FF

uint32_t *fuente = (uint32_t*) SOURCE_MEM;
uint32_t *destino = (uint32_t*) DEST_MEM;

GPDMA_LLI_Type cfgLLICH0 = {0};
GPDMA_Channel_CFG_Type cfgCH0 = {0};

uint32_t *vrx = (uint32_t*) ADC_MEM;
uint32_t *vry = (uint32_t*) ADC_MEM+1;

typedef enum
{
	WATER = 0,
	SHIP,
	HIT
} BS_STATUS_Type;

void cfgGPIO(void);
void cfgTimer(void);
void cfgADC(void);
void cfgDMA(void);
void cfgEINT(void);

void moveRight(void);
void moveLeft(void);
void moveUp(void);
void moveDown(void);

int main(void) {

	*fuente = (uint32_t) 55;

	cfgGPIO();
	cfgTimer();
	cfgADC();
	cfgDMA();

    while(1)
    {
    }

    return 0 ;
}

void cfgEINT(void)
{/*
	EXTI_Init();
	EXTI_PinConfig(EXTI_EINT0, EXTI_PULLUP);
	EXTI_PinConfig(EXTI_EINT1, EXTI_PULLDOWN);
	EXTI_PinConfig(EXTI_EINT2, EXTI_PULLDOWN);

	EXTI_CFG_Type cfgEXTI = {0};
	cfgEXTI.line = EXTI_EINT0;
	cfgEXTI.mode = EXTI_EDGE_SENSITIVE;
	cfgEXTI.polarity = EXTI_FALLING_EDGE;

	EXTI_Config(&cfgEXTI);
	EXTI_EnableIRQ(EXTI_EINT0);

	cfgEXTI.line = EXTI_EINT1;
	EXTI_Config(&cfgEXTI);
	EXTI_EnableIRQ(EXTI_EINT1);

	cfgEXTI.line = EXTI_EINT2;
	EXTI_Config(&cfgEXTI);
	EXTI_EnableIRQ(EXTI_EINT2);

	NVIC_EnableIRQ(EINT0_IRQn);
	NVIC_EnableIRQ(EINT1_IRQn);
	NVIC_EnableIRQ(EINT2_IRQn);*/
}

void EINT0_IRQHandler(void){}

void EINT1_IRQHandler(void){}

void EINT2_IRQHandler(void){}


void cfgGPIO(void)
{
	// Se configura una respuesta visual con el led integrado
	PINSEL_CFG_Type cfgPin = {0};
	cfgPin.pinNum = PINSEL_PIN_22;
	cfgPin.pinMode = PINSEL_TRISTATE;
	cfgPin.openDrain = PINSEL_OD_NORMAL;
	cfgPin.portNum = PINSEL_PORT_0;

	PINSEL_ConfigPin(&cfgPin);
	GPIO_SetDir(GPIO_PORT_0, RED_LED, GPIO_OUTPUT);

	// Configuracion de los pines usados para UART
	// RXD0 Y TXD0
	/*
	cfgPin.pinNum = PINSEL_PIN_2;
	cfgPin.funcNum = PINSEL_FUNC_1;

	PINSEL_ConfigPin(&cfgPin);

	cfgPin.pinNum = PINSEL_PIN_3;
	PINSEL_ConfigPin(&cfgPin);
	*/
}

void cfgDMA(void)
{
	//GPDMA_LLI_Type cfgLLICH0_AD0 = {0};
	//GPDMA_LLI_Type cfgLLICH0_AD1 = {0};

	NVIC_DisableIRQ(DMA_IRQn);
	GPDMA_Init();

	uint32_t transferSize = 1;

	//GPDMA_Channel_CFG_Type cfgCH0 = {0};

	cfgCH0.channelNum = GPDMA_CHANNEL_7;
	cfgCH0.transferSize = transferSize;
	cfgCH0.srcMemAddr = (uint32_t) fuente;
	cfgCH0.dstMemAddr = (uint32_t) destino;
	cfgCH0.linkedList = 0;
	cfgCH0.transferWidth = GPDMA_WORD;
	cfgCH0.transferType = GPDMA_M2M;

	if(GPDMA_Setup(&cfgCH0) != SUCCESS)
	{
		while(1);
	}

	GPDMA_ChannelCmd(GPDMA_CHANNEL_7, ENABLE);

	NVIC_EnableIRQ(DMA_IRQn);
}

void DMA_IRQHandler(void)
{
	if(GPDMA_IntGetStatus(GPDMA_INT, GPDMA_CHANNEL_7))
	{
		if(GPDMA_IntGetStatus(GPDMA_INTTC, GPDMA_CHANNEL_7))
		{
			if(*vrx > UPPER_LIM)
			{
				//moveRight();
			}
			else if(*vrx < LOWER_LIM)
			{
				//moveLeft();
			}
			if(*vry > UPPER_LIM)
			{
				//moveUp();
			}
			else if(*vry < LOWER_LIM)
			{
				//moveDown();
			}
			GPDMA_ClearIntPending(GPDMA_CLR_INTTC, GPDMA_CHANNEL_7);
		}
		GPDMA_ClearIntPending(GPDMA_CLR_INTERR, GPDMA_CHANNEL_7);
	}
}

// Done
void cfgADC(void)
{
  ADC_Init(ADC_RATE);

  ADC_PinConfig(ADC_CHANNEL_0);
  ADC_PinConfig(ADC_CHANNEL_1);

  ADC_BurstCmd(DISABLE);

  ADC_StartCmd(ADC_START_ON_MAT01);

  ADC_EdgeStartConfig(ADC_START_ON_FALLING);

  ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

  ADC_IntConfig(ADC_CHANNEL_0, ENABLE);
  ADC_IntConfig(ADC_CHANNEL_1, ENABLE);

  NVIC_EnableIRQ(ADC_IRQn);
}
/*
void cfgADC(void)
{
  ADC_Init(ADC_RATE);
  ADC_PowerdownCmd(ENABLE);

  ADC_PinConfig(ADC_CHANNEL_0);
  ADC_PinConfig(ADC_CHANNEL_1);

  ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);
  ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

  ADC_BurstCmd(ENABLE);

  ADC_StartCmd(ADC_START_CONTINUOUS);

  //ADC_EdgeStartConfig(ADC_START_ON_FALLING);
}*/

// Done
void cfgTimer(void)
{
	TIM_TIMERCFG_Type cfgTimer = {0};
	cfgTimer.prescaleOption = TIM_USVAL;
	cfgTimer.prescaleValue = 1000;

	TIM_MATCHCFG_Type cfgMatch10 = {0},
	cfgMatch01 = {0};
	//cfgMatch02 = {0};

	// Match para el led rojo. MATCH_10
	cfgMatch10.intOnMatch = ENABLE;
	cfgMatch10.stopOnMatch = DISABLE;
	cfgMatch10.resetOnMatch = ENABLE;
	cfgMatch10.extMatchOutputType = TIM_NOTHING;
	cfgMatch10.matchValue = 999;

	TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &cfgTimer);
	TIM_ConfigMatch(LPC_TIM1, &cfgMatch10);

	cfgMatch01 = cfgMatch10;

	// Match para el ADC. MATCH_01
    cfgMatch01.intOnMatch = DISABLE;
	cfgMatch01.matchChannel = TIM_MATCH_1;
	cfgMatch01.extMatchOutputType = TIM_TOGGLE;
	cfgMatch01.matchValue = 124;

    TIM_Init(LPC_TIM0, TIM_TIMER_MODE, &cfgTimer);
	TIM_ConfigMatch(LPC_TIM0, &cfgMatch01);

	TIM_Cmd(LPC_TIM0, ENABLE);
    TIM_Cmd(LPC_TIM1, ENABLE);

	NVIC_EnableIRQ(TIMER1_IRQn);
}

// Done
void ADC_IRQHandler(void)
{
	if(ADC_GlobalGetStatus(ADC_DATA_DONE))
	{
		if(ADC_ChannelGetStatus(ADC_CHANNEL_0, ADC_DATA_DONE))
		{
			ADC_ChannelCmd(ADC_CHANNEL_0, DISABLE);
			ADC_ChannelCmd(ADC_CHANNEL_1, ENABLE);

			*vrx = ADC_ChannelGetData(ADC_CHANNEL_0);

			if(*vrx > UPPER_LIM)
			{
				//moveRight();
			} else if (*vrx < LOWER_LIM)
			{
				//moveLeft();
			}

		} else if(ADC_ChannelGetStatus(ADC_CHANNEL_1, ADC_DATA_DONE))
		{
			ADC_ChannelCmd(ADC_CHANNEL_1, DISABLE);
			ADC_ChannelCmd(ADC_CHANNEL_0, ENABLE);

			*vry = ADC_ChannelGetData(ADC_CHANNEL_1);

			if(*vry > UPPER_LIM)
			{
				//moveUp();
			} else if (*vry < LOWER_LIM)
			{
				//moveDown();
			}
		}
		//ADC_GlobalGetData();
	}
}

// Done
void TIMER1_IRQHandler(void)
{
	GPIO_TogglePins(GPIO_PORT_0, RED_LED);
	TIM_ClearIntPending(LPC_TIM1, TIM_MR0_INT);
}

void moveRight(void){}
void moveLeft(void){}
void moveUp(void){}
void moveDown(void){}





