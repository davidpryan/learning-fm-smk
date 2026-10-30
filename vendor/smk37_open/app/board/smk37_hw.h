/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#ifndef SMK37_HW_H
#define SMK37_HW_H
#include "fm1_device.h"
#include "smk37_board.h"
#include "smk37_controls.h"
#include <string.h>
typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;
#define BIT(n) (1U << (n))
#define SFR(reg,start,width,value) ((reg)=((reg)&~(((1U<<(width))-1)<<(start)))|((value)<<(start)))
#define local_irq_save(state) ((state)=fm1_device_irq_save())
#define local_irq_restore(state) fm1_device_irq_restore(state)
#define ___interrupt FM1_AUDIO_CODE __attribute__((interrupt("")))
#define SMK_REG(address) (*(volatile uint32_t *)(uintptr_t)(address))
static inline __attribute__((always_inline)) u32 smk_irq_save(void)
{
    u32 state;__asm__ volatile("%0 = icfg" : "=r"(state) :: "memory");
    u32 masked=state&~BIT(8);__asm__ volatile("icfg = %0" :: "r"(masked) : "memory");
    return state;
}
static inline __attribute__((always_inline)) void smk_irq_restore(u32 state)
{__asm__ volatile("icfg = %0" :: "r"(state) : "memory");}
#define fm1_device_irq_save smk_irq_save
#define fm1_device_irq_restore smk_irq_restore
#define fm1_device_ticks() SMK_REG(0x10804U)
#define SMK_SOFT_SET SMK_REG(0x1eef1a0U)
#define SMK_SOFT_CLEAR SMK_REG(0x1eef1a4U)
struct smk_port { volatile u32 OUT,IN,DIR,DIE,PU,PD,HD0,HD,DIEH; };
struct smk_spi { volatile u32 CON,BAUD,BUF,ADR,CNT; };
struct smk_adc { volatile u32 CON,RES; };
struct smk_iomap { volatile u32 CON0,CON1; };
struct smk_ana { volatile u32 WLA_CON0; };
#define JL_PORTA ((struct smk_port *)0x50000U)
#define JL_PORTB ((struct smk_port *)0x50040U)
#define JL_PORTC ((struct smk_port *)0x50080U)
#define JL_PORTH ((struct smk_port *)0x501c0U)
#define JL_IOMAP ((struct smk_iomap *)0x5101cU)
#define JL_SPI1 ((struct smk_spi *)0x11d00U)
#define JL_SPI2 ((struct smk_spi *)0x11e00U)
#define JL_ADC ((struct smk_adc *)0x13100U)
#define JL_ANA ((struct smk_ana *)0x11900U)
#define IO_PORTA_00 0U
#define IO_PORTA_01 1U
#define IO_PORTA_02 2U
#define IO_PORTA_03 3U
#define IO_PORTA_04 4U
#define IO_PORTA_05 5U
#define IO_PORTA_06 6U
#define IO_PORTA_07 7U
#define IO_PORTA_08 8U
#define IO_PORTA_09 9U
#define IO_PORTA_10 10U
#define IO_PORTC_09 41U
#define IO_PORTH_06 118U
#define IO_PORTH_08 120U
#define IO_PORTH_09 121U
#define IO_PORT_USB_DPB 150U
#define IO_PORT_USB_DMB 151U
#define AD_CH_PA08 1U
#define AD_CH_PA10 2U
#define AD_CH_PC09 8U
#define AD_CH_PMU 15U
#define AD_CH_VBAT ((5U<<16)|15U)
#define AD_CH_PMU_VBG 15U
#define gpio_direction_input smk37_gpio_direction_input
#define gpio_direction_output smk37_gpio_direction_output
#define gpio_set_direction smk37_gpio_set_direction
#define gpio_set_die smk37_gpio_set_die
#define gpio_set_pull_up smk37_gpio_set_pull_up
#define gpio_set_pull_down smk37_gpio_set_pull_down
#define adc_pmu_detect_en smk37_adc_pmu_detect_en
#define KEY_CNT 37
#define KEY_JET 10
#define ENCODER_CNT 8
#define BTN_CNT 18
#define BTN_JET 2
#define PAD_CNT 16
#define PADKEY_JUDGE 4
#define PADKEY_DNJET 12
#define PADKEY_UPJET 24
#define PADKEY_INIT_DNGATE 100
#define PADADC_JUDGE 10
#define PADADC_JET_OFFSET 4
#define PADADC_JET (1<<PADADC_JET_OFFSET)
#define MP_ADC_JET 5
#define MP_ADC_CNT_JET 8
#define PEDAL_ADC_JET 6
#define PEDAL_ADC_CNT_JET 8
#define FAST_ADC_CNT 18
#define SLOW_ADC_CNT 8
#define SMK_MSG(type,id,value) (((u32)(type)<<24)|((u32)(id)<<16)|(u16)(value))
#define BTN_MSG(id,v) SMK_MSG(SMK37_BUTTON,id,v)
#define KEY_MSG(id,v) SMK_MSG(SMK37_KEY,id,v)
#define PADADC_MSG(id,v) SMK_MSG(SMK37_PRESSURE,id,v)
#define PADKEY_MSG(id,v) SMK_MSG(SMK37_PAD,id,v)
#define ENCODER_MSG(id,v) SMK_MSG(SMK37_ENCODER,id,v)
#define PEDAL_MSG(id,v) SMK_MSG(SMK37_PEDAL,id,v)
#define MP_MSG(id,v) SMK_MSG(SMK37_FADER,id,v)
void smk37_scan_init(void);
void smk37_scan_read(void);
u16 smk37_battery_adc(void);
void smk37_led(unsigned id, unsigned state);
void smk37_rgb_service(void);
void smk37_rgb_set(unsigned id, unsigned rgb);
#endif
