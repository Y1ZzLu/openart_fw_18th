/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include "cfg_mux_mgr.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "fsl_iomuxc.h"
#include "modled.h"
#include "pin_defs_mcu.h"
//#include "genhdr/pins.h"
#include "cfg_mux_mgr.h"
#if (MICROPY_HW_LED_NUM==1)
#define MICROPY_HW_LED1
#elif (MICROPY_HW_LED_NUM==2)
#define MICROPY_HW_LED1
#define MICROPY_HW_LED2
#elif (MICROPY_HW_LED_NUM==3)
#define MICROPY_HW_LED1
#define MICROPY_HW_LED2
#define MICROPY_HW_LED3
#elif (MICROPY_HW_LED_NUM==4)
#define MICROPY_HW_LED1
#define MICROPY_HW_LED2
#define MICROPY_HW_LED3
#define MICROPY_HW_LED4
#endif


#if defined(MICROPY_HW_LED1)

/// \moduleref pyb
/// \class LED - LED object
///
/// The LED object controls an individual LED (Light Emitting Diode).

// the default is that LEDs are not inverted, and pin driven high turns them on
#ifndef MICROPY_HW_LED_INVERTED
#define MICROPY_HW_LED_INVERTED (0)
#endif

typedef struct _pyb_led_obj_t {
    mp_obj_base_t base;
    mp_uint_t led_id;
    const pin_obj_t *led_pin;
    MuxItem_t mux;
} pyb_led_obj_t;

#define NUM_LEDS MICROPY_HW_LED_NUM
pyb_led_obj_t pyb_led_obj[NUM_LEDS] = {
    {{&pyb_led_type}, 1, NULL},
#if defined(MICROPY_HW_LED2)
    {{&pyb_led_type}, 2, NULL},
#if defined(MICROPY_HW_LED3)
    {{&pyb_led_type}, 3, NULL},
#if defined(MICROPY_HW_LED4)
    {{&pyb_led_type}, 4, NULL},
#endif
#endif
#endif
};

#if defined(MICROPY_HW_LED1_PWM) \
    || defined(MICROPY_HW_LED2_PWM) \
    || defined(MICROPY_HW_LED3_PWM) \
    || defined(MICROPY_HW_LED4_PWM)

// The following is semi-generic code to control LEDs using PWM.
// It currently supports TIM1, TIM2 and TIM3, channels 1-4.
// Configure by defining the relevant MICROPY_HW_LEDx_PWM macros in mpconfigboard.h.
// If they are not defined then PWM will not be available for that LED.

#define LED_PWM_ENABLED (1)

#ifndef MICROPY_HW_LED1_PWM
#define MICROPY_HW_LED1_PWM { NULL, 0, 0, 0 }
#endif
#ifndef MICROPY_HW_LED2_PWM
#define MICROPY_HW_LED2_PWM { NULL, 0, 0, 0 }
#endif
#ifndef MICROPY_HW_LED3_PWM
#define MICROPY_HW_LED3_PWM { NULL, 0, 0, 0 }
#endif
#ifndef MICROPY_HW_LED4_PWM
#define MICROPY_HW_LED4_PWM { NULL, 0, 0, 0 }
#endif

#define LED_PWM_TIM_PERIOD (10000) // TIM runs at 1MHz and fires every 10ms

typedef struct _led_pwm_config_t {
	union{
		GPT_Type *pGPT;
		TMR_Type *pTMR;
		PWM_Type *pPWM;
	};
	uint8_t tim_type; // 0 = GPT, 1 = TMR, 2 = (Flex)PWM
    uint8_t tim_id;
    uint8_t tim_channel;	// encoded channel, different timer has different interpretation
    uint8_t alt_func;	// alt func index of pin mux
} led_pwm_config_t;

const led_pwm_config_t led_pwm_config[] = {
    MICROPY_HW_LED1_PWM,
    MICROPY_HW_LED2_PWM,
    MICROPY_HW_LED3_PWM,
    MICROPY_HW_LED4_PWM,
};

STATIC uint8_t led_pwm_state = 0;

static inline bool led_pwm_is_enabled(int led) {
    return (led_pwm_state & (1 << led)) != 0;
}

// this function has a large stack so it should not be inlined
void led_pwm_init(int led) __attribute__((noinline));
void led_pwm_init(int led) {
    // const pin_obj_t *led_pin = pyb_led_obj[led - 1].led_pin;

    // >>> rocky: todos
	// <<<
    // indicate that this LED is using PWM
    led_pwm_state |= 1 << led;
}

STATIC void led_pwm_deinit(int led) {
    // make the LED's pin a standard GPIO output pin
    const pin_obj_t *led_pin = pyb_led_obj[led - 1].led_pin;
    led_pin = led_pin;
}

#else
#define LED_PWM_ENABLED (0)
#endif
void led_obj_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_led_obj_t *self = self_in;
    mp_printf(print, "LED(%lu)", self->led_id);
}
/// \classmethod \constructor(id)
/// Create an LED object associated with the given LED:
///
///   - `id` is the LED number, 1-4.
STATIC mp_obj_t led_obj_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    // get led number
    mp_int_t led_id = mp_obj_get_int(args[0]);

    // check led number
    if (!(1 <= led_id && led_id <= NUM_LEDS)) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "LED(%d) does not exist", led_id));
    }

    pyb_led_obj_t* self;
    // create new UART object  
    self =  gc_alloc(sizeof(*self), GC_ALLOC_FLAG_HAS_FINALISER);
    Mux_Take(self, "led", led_id, "-", &self->mux);
	
    if ((self->mux.pPinObj == mp_const_none) || (mp_obj_is_small_int(self->mux.pPinObj)&&(mp_obj_get_int(self->mux.pPinObj) == 0))) {
        /* downward compatible with previous hard-coded pin object mapping
        drop the allocated pyb_led_obj_t instance to GC */
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "LED(%d) PinObj is Null, error in cmm_cfg.cvs", led_id));
		#if 0
        self = &pyb_led_obj[0];
        // nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "LED(%d) can't take reqruied pin", led_id));
        self->mux.pPinObj = self->led_pin;
        strcpy(self->mux.szComboKey, "-");
        strcpy(self->mux.szHint, "-");
		mp_hal_ConfigGPIO(self->mux.pPinObj, GPIO_MODE_OUTPUT_PP, 1);
        return self;            
		#endif
    } 
    self->base.type = &pyb_led_type;
    self->led_id = led_id;    
    self->led_pin = self->mux.pPinObj;
#ifdef BSP_USING_74HC595
	const pin_obj_t *led_pin = self->mux.pPinObj;
	if(led_pin->af->fn== AF_FN_HC595)
	{
		return self;
	}
#endif	
	mp_hal_ConfigGPIO(self->mux.pPinObj, GPIO_MODE_OUTPUT_PP, 1);		
    // return static led object
    return self;
}
//#define LED_PIN               GET_PIN(self->led_pin->port, self->led_pin->pin)
/// \method on()
/// Turn the LED on.
mp_obj_t led_obj_on(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
#ifdef BSP_USING_74HC595
	const pin_obj_t *led_pin = self->mux.pPinObj;
	if(led_pin->af->fn== AF_FN_HC595)
	{
		drv_hc595_gpio_write(led_pin->pin,1);
		return mp_const_none;
	}
#endif		
	
	mp_hal_pin_write(self->led_pin,1);
    return mp_const_none;
}

/// \method off()
/// Turn the LED of
mp_obj_t led_obj_off(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
#ifdef BSP_USING_74HC595
	const pin_obj_t *led_pin = self->mux.pPinObj;
	if(led_pin->af->fn== AF_FN_HC595)
	{
		drv_hc595_gpio_write(led_pin->pin,0);
		return mp_const_none;
	}
#endif
	mp_hal_pin_write(self->led_pin,0);
    return mp_const_none;
}

/// \method toggle()
/// Toggle the LED between on and off.
mp_obj_t led_obj_toggle(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
#ifdef BSP_USING_74HC595
	const pin_obj_t *led_pin = self->mux.pPinObj;
	if(led_pin->af->fn== AF_FN_HC595)
	{
		int ret = drv_hc595_gpio_read(led_pin->pin);
		drv_hc595_gpio_write(led_pin->pin,!ret);
		return mp_const_none;
	}
#endif	
	int ret = mp_hal_pin_read(self->led_pin);
	mp_hal_pin_write(self->led_pin, !ret);

    return mp_const_none;
}

mp_obj_t led_obj_del(mp_obj_t self_in) {
    pyb_led_obj_t *self = self_in;
    Mux_Give(&self->mux);
    return mp_const_none;

}

/// \method intensity([value])
/// Get or set the LED intensity.  Intensity ranges between 0 (off) and 255 (full on).
/// If no argument is given, return the LED intensity.
/// If an argument is given, set the LED intensity and return `None`.
mp_obj_t led_obj_intensity(mp_uint_t n_args, const mp_obj_t *args) {
    pyb_led_obj_t *self = args[0];
    if (n_args == 1) {
        return mp_obj_new_int(0);
    } else {
		if(mp_obj_get_int(args[1])>0)
		GPIO_PinWrite(self->led_pin->gpio, self->led_pin->pin, 1);
		else
		GPIO_PinWrite(self->led_pin->gpio, self->led_pin->pin, 0);
        return mp_const_none;
    }
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(led_obj_on_obj, led_obj_on);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(led_obj_off_obj, led_obj_off);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(led_obj_toggle_obj, led_obj_toggle);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(led_obj_del_obj, led_obj_del);
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(led_obj_intensity_obj, 1, 2, led_obj_intensity);

STATIC const mp_rom_map_elem_t led_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&led_obj_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&led_obj_off_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&led_obj_del_obj) },
    { MP_ROM_QSTR(MP_QSTR_toggle), MP_ROM_PTR(&led_obj_toggle_obj) },
    { MP_ROM_QSTR(MP_QSTR_intensity), MP_ROM_PTR(&led_obj_intensity_obj) },
};

STATIC MP_DEFINE_CONST_DICT(led_locals_dict, led_locals_dict_table);

const mp_obj_type_t pyb_led_type = {
    { &mp_type_type },
    .name = MP_QSTR_LED,
    .print = led_obj_print,
    .make_new = led_obj_make_new,
    .locals_dict = (mp_obj_dict_t*)&led_locals_dict,
};
#endif  // defined(MICROPY_HW_LED1)
