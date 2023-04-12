/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

// these states correspond to values from query_irq, enable_irq and disable_irq
#define IRQ_STATE_DISABLED (0x00000001)
#define IRQ_STATE_ENABLED  (0x00000000)

// Enable this to get a count for the number of times each irq handler is called,
// accessible via pyb.irq_stats().
#define IRQ_ENABLE_STATS (0)

#if IRQ_ENABLE_STATS
extern uint32_t irq_stats[SMARTCARD1_IRQn + 1];
#define IRQ_ENTER(irq) ++irq_stats[irq]
#define IRQ_EXIT(irq)
#else
#define IRQ_ENTER(irq)
#define IRQ_EXIT(irq)
#endif

// enable_irq and disable_irq are defined inline in mpconfigport.h

#if __CORTEX_M >= 0x03

// irqs with a priority value greater or equal to "pri" will be disabled
// "pri" should be between 1 and 15 inclusive
static inline uint32_t raise_irq_pri(uint32_t pri) {
    uint32_t basepri = __get_BASEPRI();
    // If non-zero, the processor does not process any exception with a
    // priority value greater than or equal to BASEPRI.
    // When writing to BASEPRI_MAX the write goes to BASEPRI only if either:
    //   - Rn is non-zero and the current BASEPRI value is 0
    //   - Rn is non-zero and less than the current BASEPRI value
    pri <<= (8 - __NVIC_PRIO_BITS);
	  __set_BASEPRI_MAX(pri);
    return basepri;
}

// "basepri" should be the value returned from raise_irq_pri
static inline void restore_irq_pri(uint32_t basepri) {
    __set_BASEPRI(basepri);
}

#endif

// IRQ priority definitions.
//
// Lower number implies higher interrupt priority.
//
// The default priority grouping is set to NVIC_PRIORITYGROUP_4 in the
// HAL_Init function. This corresponds to 4 bits for the priority field
// and 0 bits for the sub-priority field (which means that for all intensive
// purposes that the sub-priorities below are ignored).
//
// While a given interrupt is being processed, only higher priority (lower number)
// interrupts will preempt a given interrupt. If sub-priorities are active
// then the sub-priority determines the order that pending interrupts of
// a given priority are executed. This is only meaningful if 2 or more
// interrupts of the same priority are pending at the same time.
//
// The priority of the SysTick timer is determined from the TICK_INT_PRIORITY
// value which is normally set to 0 in the stm32f4xx_hal_conf.h file.
//
// The following interrupts are arranged from highest priority to lowest
// priority to make it a bit easier to figure out.

//                           Priority   Sub-Priority
//                           --------   ------------
//#def  IRQ_PRI_SYSTICK         0
//#def  IRQ_SUBPRI_SYSTICK                  0

// csi irq frequency can be very high, so make it priority high
#define IRQ_PRI_CSI             0
#define IRQ_SUBPRI_CSI                      0

// make high enough to do profiling
#define IRQ_PRI_SYSTICK         1
#define IRQ_SUBPRI_SYSTICK                  0

// Flash IRQ must be higher priority than interrupts of all those components
// that rely on the flash storage.
#define IRQ_PRI_FLASH           2
#define IRQ_SUBPRI_FLASH                    0

// SDIO must be higher priority than USB
#define IRQ_PRI_SDIO            4
#define IRQ_SUBPRI_SDIO                     0

#define IRQ_PRI_CAN             7
#define IRQ_SUBPRI_CAN                      0

// Interrupt priority for non-special timers.
#define IRQ_PRI_TIMX            10
#define IRQ_SUBPRI_TIMX                     0

#define IRQ_PRI_EXTINT          10
#define IRQ_SUBPRI_EXTINT                   0

#define IRQ_PRI_UART            10
#define IRQ_SUBPRI_UART                     0

// USB IRQ can take long time to complete, so make priority low
#define IRQ_PRI_USB_OTG1        12
#define IRQ_SUBPRI_OTG_FS                   0

#define IRQ_PRI_RTC_WKUP        14
#define IRQ_SUBPRI_RTC_WKUP                 0
// PENDSV should be at the lowst priority so that other interrupts complete
// before exception is raised.
#define IRQ_PRI_PENDSV          15
#define IRQ_SUBPRI_PENDSV                   0

#ifndef _ISR_C_
extern 
#endif
volatile uint32_t s_mpySignalCode;
#include <rtthread.h>
#ifdef SOC_IMXRT1170_SERIES
#define MPPORT_SIGNAL_IRQn  Reserved186_IRQn
#define MPPORT_SIGNAL_HANDLER Reserved186_IRQHandler
#else
#define MPPORT_SIGNAL_IRQn  Reserved86_IRQn
#define MPPORT_SIGNAL_HANDLER Reserved86_IRQHandler
#endif
typedef enum {
	mpportsignal_tick = 0,
	mpportsignal_longjmp = 1,
}MpPortSignalCode_t;
#define MPPORT_SEND_SIGNAL(signalCode) do {\
	s_mpySignalCode = (signalCode) ;  \
	NVIC->STIR = MPPORT_SIGNAL_IRQn; \
}while(0)

