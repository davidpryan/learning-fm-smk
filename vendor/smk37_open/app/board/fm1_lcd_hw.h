/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Copyright (c) 2026 M-VAVE.
 * AC791N SPI1 and Pro/Elite display pin setup. */
#pragma once
#include "fm1_gpio.h"
#define LCD_REGISTER(offset) (*(volatile uint32_t *)(0x11d00u+(offset)))
enum { LCD_ENABLE=1, LCD_MASTER=1u<<5, LCD_ACK=1u<<14, LCD_DONE=1u<<15,
       LCD_SELECT_PIN=1u<<8, LCD_DATA_PIN=1u<<5 };
static uint32_t fm1_lcd_timeouts;
static inline void fm1_lcd_deselect(void) {FM1_PR(FM1_PC,FM1_OUT)|=LCD_SELECT_PIN;}
static inline void fm1_lcd_baud(uint32_t divider) {LCD_REGISTER(4)=divider;}
static inline unsigned fm1_lcd_ready(void) {return (LCD_REGISTER(0)&LCD_DONE)!=0;}
static void fm1_lcd_hw_init(void)
{
    smk37_gpio_direction_output(40,1); /* PC8 LCD CS */
    smk37_gpio_set_direction(21,0);    /* PB5 LCD DC */
    smk37_gpio_set_direction(22,0);    /* PB6 SPI clock */
    smk37_gpio_set_direction(23,1);    /* PB7 shared SPI data route */
    smk37_gpio_direction_output(42,0); /* PC10 RGB route */
    *(volatile uint32_t *)0x51020u&=~(1u<<4);
    LCD_REGISTER(4)=4;LCD_REGISTER(0)=LCD_ENABLE|LCD_MASTER|LCD_ACK;
}
static void fm1_lcd_recover(void)
{
    LCD_REGISTER(0)=0;
    __asm__ volatile("csync" ::: "memory");
    LCD_REGISTER(16)=0;fm1_lcd_deselect();
    LCD_REGISTER(0)=LCD_ENABLE|LCD_MASTER|LCD_ACK;++fm1_lcd_timeouts;
}
static void fm1_lcd_wait(void)
{
    for(unsigned limit=4000000;limit;--limit) {
        if(fm1_lcd_ready()) {LCD_REGISTER(0)|=LCD_ACK;return;}
    }
    fm1_lcd_recover();
}
static void fm1_lcd_send_cmd(uint8_t command)
{
    FM1_PR(FM1_PB,FM1_OUT)&=~LCD_DATA_PIN;
    FM1_PR(FM1_PC,FM1_OUT)&=~LCD_SELECT_PIN;
    LCD_REGISTER(0)|=LCD_ACK;LCD_REGISTER(8)=command;
}
static void fm1_lcd_send_data(const void *data,uint32_t bytes)
{
    FM1_PR(FM1_PB,FM1_OUT)|=LCD_DATA_PIN;
    FM1_PR(FM1_PC,FM1_OUT)&=~LCD_SELECT_PIN;
    LCD_REGISTER(0)|=LCD_ACK;
    __asm__ volatile("csync" ::: "memory");
    LCD_REGISTER(12)=(uint32_t)(uintptr_t)data;LCD_REGISTER(16)=bytes;
}
