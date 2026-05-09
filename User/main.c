#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "BlueTooth.h"
#include "Servo.h"
#include "PetAction.h"
#include "Face_Config.h"
#include "PWM.h"


/***************************************************************************************
  * ???????angels_wyh???????????????????
  * ?????????????????????????
  * 
  * ?????            ??STM32F103C8T6???????????
  * ????????        2026.3.23
  * 
  ***************************************************************************************/


uint16_t Time;
uint16_t HuXi;
uint16_t PanDuan = 1;
uint16_t Wait = 0;

int main(void)
{

    Servo_Init();
    OLED_Init(); // OLED???
    OLED_ShowImage(0, 0, 128, 64, Face_sleep);
    OLED_Update();
    BlueTooth_Init(); // ?????
    while (1)
    {
        BlueTooth_Poll();

        if(Action_Mode==0){Action_relaxed_getdowm();WServo_Angle(90);} // ????
        else if(Action_Mode==1){Action_sit();} // ??
        else if(Action_Mode==2){Action_upright();} // ??
        else if(Action_Mode==3){Action_getdowm();} // ??
        else if(Action_Mode==4){Action_advance();} // ??
        else if(Action_Mode==5){Action_back();} // ??
        else if(Action_Mode==6){Action_Lrotation();} // ??
        else if(Action_Mode==7){Action_Rrotation();} // ??
        else if(Action_Mode==8){Action_Swing();} // ??
        else if(Action_Mode==9){Action_SwingTail();} // ???
        else if(Action_Mode==10){Action_JumpU();} // ??
        else if(Action_Mode==11){Action_JumpD();} // ??
        else if(Action_Mode==12){Action_upright2();} // ????2
        else if(Action_Mode==13){Action_Hello();} // ???
        else if(Action_Mode==14){Action_stretch();} // ???
        else if(Action_Mode==15){Action_Lstretch();} // ????
    }
}


void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        if (AllLed == 1 && BreatheLed == 0)
        {
            PWM_LED1(20000);
            PWM_LED2(20000);
        }
        else if (AllLed == 1 && BreatheLed == 1)
        {
            if (PanDuan == 1)
            {
                HuXi += 100;
                PWM_LED1(HuXi);
                PWM_LED2(HuXi);
                if (HuXi == 20000)
                    PanDuan = 2;
            }
            else if (PanDuan == 2)
            {
                HuXi -= 100;
                PWM_LED1(HuXi);
                PWM_LED2(HuXi);
                if (HuXi == 0)
                {
                    PanDuan = 3;
                }
            }
            else if (PanDuan == 3)
            {
                Wait += 1000;
                if (Wait == 20000)
                {
                    PanDuan = 1;
                    Wait = 0;
                }
            }
        }
        else if (AllLed == 0)
        {
            PWM_LED1(0);
            PWM_LED2(0);
        }

        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}
