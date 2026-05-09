#include "stm32f10x.h"                  // Device header
#include "BlueTooth.h"
#include "Delay.h"
#include "PWM.h"
#include "PetAction.h"
#include "Face_Config.h"
#include <string.h>
#include <ctype.h>

volatile uint16_t AllLed = 1;
volatile uint16_t BreatheLed = 0;
volatile uint16_t Sustainedmove = 0;

volatile uint16_t Action_Mode = 0;
volatile uint16_t SpeedDelay = 200;
volatile uint16_t SwingDelay = 6;
volatile uint16_t Face_Mode = 0;
volatile uint8_t WeiBa = 0;
volatile uint8_t BlueTooth_ATReady = 0;
volatile uint8_t BlueTooth_NameReady = 0;
char BlueTooth_Name[32] = {0};

static uint16_t bluetoothIgnoreTicks = 0;
static char bluetoothAtBuffer[64];
static uint8_t bluetoothAtLength = 0;
static uint8_t bluetoothCommandEnabled = 0;
static volatile uint16_t bluetoothStartupDelayMs = 1000;
static uint8_t bluetoothATTried = 0;
static uint32_t bluetoothNameSeed = 0x13579BDF;

static uint8_t IsValidCommand(uint8_t cmd)
{
    return (cmd >= 0x29 && cmd <= 0x49);
}

static void FlushUsartErrors(USART_TypeDef *USARTx)
{
    if (USART_GetFlagStatus(USARTx, USART_FLAG_ORE) == SET ||
        USART_GetFlagStatus(USARTx, USART_FLAG_NE) == SET ||
        USART_GetFlagStatus(USARTx, USART_FLAG_FE) == SET ||
        USART_GetFlagStatus(USARTx, USART_FLAG_PE) == SET)
    {
        volatile uint16_t status = USARTx->SR;
        volatile uint16_t data = USARTx->DR;
        (void)status;
        (void)data;
    }
}

static void BlueTooth_SendByte(uint8_t data)
{
    USART_SendData(USART3, data);
    while (USART_GetFlagStatus(USART3, USART_FLAG_TC) == RESET)
    {
    }
}

static void BlueTooth_SendString(const char *str)
{
    while (*str != '\0')
    {
        BlueTooth_SendByte((uint8_t)(*str));
        str++;
    }
}

static void BlueTooth_ResetAtBuffer(void)
{
    bluetoothAtLength = 0;
    bluetoothAtBuffer[0] = '\0';
}

static void BlueTooth_DrainRx(void)
{
    FlushUsartErrors(USART3);
    while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET)
    {
        (void)USART_ReceiveData(USART3);
    }
}

static void BlueTooth_ReadPendingBytes(void)
{
    FlushUsartErrors(USART3);
    while (USART_GetFlagStatus(USART3, USART_FLAG_RXNE) == SET)
    {
        char ch = (char)(USART_ReceiveData(USART3) & 0xFF);
        if (bluetoothAtLength < (sizeof(bluetoothAtBuffer) - 1U))
        {
            bluetoothAtBuffer[bluetoothAtLength++] = ch;
            bluetoothAtBuffer[bluetoothAtLength] = '\0';
        }
    }
}

static uint8_t BlueTooth_ResponseContains(const char *text)
{
    return (strstr(bluetoothAtBuffer, text) != 0);
}

static uint8_t BlueTooth_CollectResponse(uint16_t timeoutMs)
{
    while (timeoutMs--)
    {
        BlueTooth_ReadPendingBytes();
        if (BlueTooth_ResponseContains("OK\r\n"))
        {
            return 1;
        }
        Delay_ms(1);
    }
    return 0;
}

static uint8_t BlueTooth_SendATCommand(const char *command, uint16_t timeoutMs)
{
    BlueTooth_ResetAtBuffer();
    BlueTooth_DrainRx();
    BlueTooth_SendString(command);
    BlueTooth_SendString("\r\n");
    return BlueTooth_CollectResponse(timeoutMs);
}

static uint8_t BlueTooth_ReadName(char *nameBuffer, uint8_t nameBufferSize)
{
    char *start;
    char *end;
    uint8_t length;

    start = strstr(bluetoothAtBuffer, "+NAME:");
    if (start == 0)
    {
        return 0;
    }

    start += 6;
    end = strstr(start, "\r\n");
    if (end == 0 || end <= start)
    {
        return 0;
    }

    length = (uint8_t)(end - start);
    if (length >= nameBufferSize)
    {
        length = (uint8_t)(nameBufferSize - 1U);
    }

    memcpy(nameBuffer, start, length);
    nameBuffer[length] = '\0';
    return 1;
}

static uint32_t BlueTooth_NextRandom(void)
{
    bluetoothNameSeed = bluetoothNameSeed * 1103515245UL + 12345UL;
    return bluetoothNameSeed;
}

static uint8_t BlueTooth_IsLowerAlphaNum(char ch)
{
    return ((ch >= 'a') && (ch <= 'z')) || ((ch >= '0') && (ch <= '9'));
}

static uint8_t BlueTooth_NameMatchesRule(const char *name)
{
    uint8_t i;
    uint8_t prefixLen = (uint8_t)strlen(BLUETOOTH_NAME_PREFIX);

    if (strlen(name) != (size_t)(prefixLen + BLUETOOTH_NAME_SUFFIX_LEN))
    {
        return 0;
    }

    if (strncmp(name, BLUETOOTH_NAME_PREFIX, prefixLen) != 0)
    {
        return 0;
    }

    for (i = 0; i < BLUETOOTH_NAME_SUFFIX_LEN; i++)
    {
        if (!BlueTooth_IsLowerAlphaNum(name[prefixLen + i]))
        {
            return 0;
        }
    }

    return 1;
}

static void BlueTooth_GenerateTargetName(char *nameBuffer, uint8_t nameBufferSize)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    uint8_t prefixLen = (uint8_t)strlen(BLUETOOTH_NAME_PREFIX);
    uint8_t i;

    if (nameBufferSize <= (uint8_t)(prefixLen + BLUETOOTH_NAME_SUFFIX_LEN))
    {
        if (nameBufferSize > 0)
        {
            nameBuffer[0] = '\0';
        }
        return;
    }

    strcpy(nameBuffer, BLUETOOTH_NAME_PREFIX);
    for (i = 0; i < BLUETOOTH_NAME_SUFFIX_LEN; i++)
    {
        nameBuffer[prefixLen + i] = charset[BlueTooth_NextRandom() % (sizeof(charset) - 1U)];
    }
    nameBuffer[prefixLen + BLUETOOTH_NAME_SUFFIX_LEN] = '\0';
}

static void BlueTooth_ConfigName(void)
{
    char targetName[32];
    char commandBuffer[48];

    BlueTooth_ATReady = 0;
    BlueTooth_NameReady = 0;
    BlueTooth_Name[0] = '\0';
    bluetoothCommandEnabled = 0;
    USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);
    BlueTooth_DrainRx();

    if (!BlueTooth_SendATCommand("AT", 5000))
    {
        goto config_exit;
    }
    BlueTooth_ATReady = 1;

    if (!BlueTooth_SendATCommand("AT+NAME?", 5000))
    {
        goto config_exit;
    }

    if (!BlueTooth_ReadName(BlueTooth_Name, sizeof(BlueTooth_Name)))
    {
        goto config_exit;
    }

    BlueTooth_NameReady = 1;
    if (BlueTooth_NameMatchesRule(BlueTooth_Name))
    {
        goto config_exit;
    }

    BlueTooth_GenerateTargetName(targetName, sizeof(targetName));
    strcpy(commandBuffer, "AT+NAME=");
    strcat(commandBuffer, targetName);
    if (BlueTooth_SendATCommand(commandBuffer, 5000))
    {
        strcpy(BlueTooth_Name, targetName);
        BlueTooth_NameReady = 1;
    }

config_exit:
    BlueTooth_DrainRx();
    bluetoothCommandEnabled = 1;
    USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
}

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
    USART_ITConfig(USART3, USART_IT_RXNE, DISABLE);

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
    bluetoothCommandEnabled = 1;
    bluetoothStartupDelayMs = 1000;
    bluetoothATTried = 0;
}

void BlueTooth_Poll(void)
{
    FlushUsartErrors(USART3);
    if (bluetoothIgnoreTicks > 0)
    {
        bluetoothIgnoreTicks--;
    }

    if ((bluetoothATTried == 0) && (bluetoothStartupDelayMs == 0))
    {
        bluetoothATTried = 1;
        BlueTooth_ConfigName();
    }
}

void BlueTooth_TimerTick(void)
{
    if (bluetoothStartupDelayMs > 0)
    {
        bluetoothStartupDelayMs--;
    }
}

void USART1_IRQHandler(void)
{
    FlushUsartErrors(USART1);
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t cmd = (uint8_t)(USART_ReceiveData(USART1) & 0xFF);
        if (IsValidCommand(cmd))
        {
            Sustainedmove = 0;
            HandleCommand(cmd);
        }
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART3_IRQHandler(void)
{
    FlushUsartErrors(USART3);
    if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
    {
        uint8_t cmd = (uint8_t)(USART_ReceiveData(USART3) & 0xFF);
        if (bluetoothCommandEnabled && (bluetoothIgnoreTicks == 0) && IsValidCommand(cmd))
        {
            Sustainedmove = 0;
            HandleCommand(cmd);
        }
        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}
