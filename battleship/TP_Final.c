#include "LPC17xx.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_gpdma.h"

#define PIN_22	((uint32_t)(1<<22))

uint8_t inicioJuego = 0X00;
uint8_t	pausa		= 0X01;
uint8_t jugador1	= 0X02;
uint8_t jugador2	= 0X03;
uint8_t ganadorJ1	= 0X04;
uint8_t ganadorJ2	= 0X05;
uint8_t data 		= 0x06;
GPDMA_LLI_Type 			cfg_UART0_LLI_CH7;

void cfgGPIO(void);
void UART0_Init(void);
void cfgDMA_UART0_TX_Init(void);
void cfgUART0(void);
void delay(void);

int main(void) {

	cfgGPIO();
	cfgUART0();
	cfgDMA_UART0_TX_Init();

    while(1) {
    	delay();
    	data = jugador1;
    	delay();
    	data = jugador2;
    	delay();
    	data = ganadorJ1;
    	delay();
    	data = ganadorJ2;

    }

    return 0 ;
}

void cfgGPIO(void){

	PINSEL_CFG_Type	cfgPinTXD0;

	//Configuracion de pin 0.2 como TX
	cfgPinTXD0.portNum 	= PINSEL_PORT_0;
	cfgPinTXD0.pinNum	= PINSEL_PIN_2;
	cfgPinTXD0.funcNum 	= PINSEL_FUNC_1;
	cfgPinTXD0.openDrain = PINSEL_OD_NORMAL;

	//Configuracion pin led
	PINSEL_CFG_Type cfgPinLed;
	cfgPinLed.portNum  	= PINSEL_PORT_0;
	cfgPinLed.pinNum  	= PINSEL_PIN_22;
	cfgPinLed.funcNum	= PINSEL_FUNC_0;
	cfgPinLed.openDrain = PINSEL_OD_NORMAL;
	cfgPinLed.pinMode	= PINSEL_TRISTATE,

	GPIO_SetDir(GPIO_PORT_0, PIN_22, GPIO_OUTPUT);
	PINSEL_ConfigPin(&cfgPinTXD0);
	PINSEL_ConfigPin(&cfgPinLed);
}

void cfgUART0(void)
{
    // P0.2 -> TXD0 ; P0.3 -> RXD0
    LPC_PINCON->PINSEL0 |= (1 << 4) | (1 << 6);

    LPC_SC->PCONP |= (1 << 3);         // Power UART0

    uint32_t PCLK = SystemCoreClock / 4;
    uint32_t DL = PCLK / (16 * 9600);

    LPC_UART0->LCR = 0x83;             // 8N1 + DLAB
    LPC_UART0->DLL = DL & 0xFF;
    LPC_UART0->DLM = (DL >> 8);
    LPC_UART0->LCR = 0x03;             // 8N1 sin DLAB

    LPC_UART0->FCR = 0x07;             // FIFO ON + reset
    LPC_UART0->IER = (1 << 7);         // UART0 DMA Mode
}

void cfgDMA_UART0_TX_Init(void)
{

	GPDMA_Channel_CFG_Type	cfg_DMA_CH7;

	NVIC_DisableIRQ(DMA_IRQn);
	GPDMA_Init();

	cfg_UART0_LLI_CH7.srcAddr = (uint32_t)&data;
	cfg_UART0_LLI_CH7.dstAddr = (uint32_t)&LPC_UART0->THR;
	cfg_UART0_LLI_CH7.nextLLI = (uint32_t)&cfg_UART0_LLI_CH7;
	cfg_UART0_LLI_CH7.control = (1<<0);


	cfg_DMA_CH7.channelNum 	= GPDMA_CHANNEL_7;
	cfg_DMA_CH7.srcConn 	= 0;
	cfg_DMA_CH7.dstConn		= GPDMA_UART0_Tx;
	cfg_DMA_CH7.srcMemAddr	= (uint32_t)&data;
	cfg_DMA_CH7.dstMemAddr	= (uint32_t)&LPC_UART0->THR;
	cfg_DMA_CH7.transferType =  GPDMA_M2M;
	cfg_DMA_CH7.transferSize = 1;
	cfg_DMA_CH7.transferWidth= GPDMA_BYTE;
	cfg_DMA_CH7.linkedList = (uint32_t)&cfg_UART0_LLI_CH7;
	GPDMA_Setup(&cfg_DMA_CH7);
	GPDMA_ChannelCmd(GPDMA_CHANNEL_7, ENABLE);
	LPC_GPDMA->DMACSoftSReq = (1 << 7);
}


void delay(void){

	uint32_t counter;
	for(counter = 0; counter < 6000000; counter++){};

}