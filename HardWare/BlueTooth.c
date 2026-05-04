#include "stm32f10x.h"                  // Device header
#include "PWM.h"
#include "PetAction.h"
#include "Face_Config.h"

uint16_t AllLed = 1;
uint16_t BreatheLed = 0;
uint16_t Sustainedmove = 0;

uint16_t Action_Mode = 0;
uint16_t SpeedDelay = 200;
uint16_t SwingDelay = 6;
uint16_t Face_Mode = 0;
uint8_t WeiBa = 0;

static void HandleCommand(uint8_t cmd)
{
    switch (cmd)
    {
        case 0x29:
            Face_Mode = 0;
            Face_Config();
            Action_Mode = 0;
            break;

        case 0x30:
            Face_Mode = 1;
            Face_Config();
            Action_Mode = 1;
            break;

        case 0x31:
            Face_Mode = 5;
            Face_Config();
            Action_Mode = 2;
            break;

        case 0x32:
            Face_Mode = 1;
            Face_Config();
            Action_Mode = 3;
            break;

        case 0x33:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 4;
            break;

        case 0x34:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 5;
            break;

        case 0x35:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 6;
            break;

        case 0x36:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 7;
            break;

        case 0x37:
            Face_Mode = 4;
            Face_Config();
            Action_Mode = 8;
            break;

        case 0x38:
            if (SpeedDelay == 120)
            {
                Face_Mode = 3;
                Face_Config();
            }
            if (SpeedDelay > 100)
            {
                SpeedDelay -= 20;
            }
            else
            {
                Face_Mode = 2;
                Face_Config();
                SpeedDelay = 200;
            }
            break;

        case 0x39:
            if (SwingDelay == 4)
            {
                Face_Mode = 3;
                Face_Config();
            }
            if (SwingDelay > 3)
            {
                SwingDelay--;
            }
            else
            {
                Face_Mode = 4;
                Face_Config();
                SwingDelay = 9;
            }
            break;

        case 0x40:
            WeiBa = !WeiBa;
            Face_Mode = 1;
            Face_Config();
            if (WeiBa == 1)
            {
                Action_Mode = 9;
            }
            else if (Action_Mode == 9)
            {
                Action_Mode = 2;
                Action_NoSwingTail();
            }
            break;

        case 0x41:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 10;
            break;

        case 0x42:
            Face_Mode = 2;
            Face_Config();
            Action_Mode = 11;
            break;

        case 0x43:
            Face_Mode = 6;
            Face_Config();
            Action_Mode = 13;
            break;

        case 0x44:
            AllLed = 1;
            break;

        case 0x45:
            AllLed = 0;
            break;

        case 0x46:
            BreatheLed = 1;
            break;

        case 0x47:
            BreatheLed = 0;
            break;

        case 0x48:
            Face_Mode = 6;
            Face_Config();
            Action_Mode = 14;
            break;

        case 0x49:
            Face_Mode = 6;
            Face_Config();
            Action_Mode = 15;
            break;

        default:
            break;
    }
}

void BlueTooth_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_4);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitTypeDef UASRT_InitStructure;
    UASRT_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    UASRT_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    UASRT_InitStructure.USART_Parity = USART_Parity_No;
    UASRT_InitStructure.USART_StopBits = USART_StopBits_1;
    UASRT_InitStructure.USART_WordLength = USART_WordLength_8b;
    UASRT_InitStructure.USART_BaudRate = 9600;
    USART_Init(USART1, &UASRT_InitStructure);
    USART_Init(USART3, &UASRT_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
    USART_Cmd(USART3, ENABLE);
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t cmd = (uint8_t)(USART_ReceiveData(USART1) & 0xFF);
        Sustainedmove = 0;
        HandleCommand(cmd);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
        uint8_t cmd = (uint8_t)(USART_ReceiveData(USART3) & 0xFF);
        Sustainedmove = 0;
        HandleCommand(cmd);
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}
