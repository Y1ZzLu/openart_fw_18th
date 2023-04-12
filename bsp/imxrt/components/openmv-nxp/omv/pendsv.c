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
#define _ISR_C_
#include <stdlib.h>
#include "fsl_common.h"
//#include "hal_wrapper.h"
#include "py/mpstate.h"
#include "py/runtime.h"
#include "lib/utils/interrupt_char.h"
#include "pendsv.h"
#include "irq.h"
#include "mpconfigport.h"
#undef MICROPY_PY_RTTHREAD
// This variable is used to save the exception object between a ctrl-C and the
// PENDSV call that actually raises the exception.  It must be non-static
// otherwise gcc-5 optimises it away.  It can point to the heap but is not
// traced by GC.  This is okay because we only ever set it to
// mp_kbd_exception which is in the root-pointer set.
void *pendsv_object;
static uint32_t sw_irq_count = 0;
void *sw_count_ptr = &sw_irq_count;

#define IS_NVIC_PREEMPTION_PRIORITY(PRIORITY)  ((PRIORITY) < 0x10U)

#define IS_NVIC_SUB_PRIORITY(PRIORITY)         ((PRIORITY) < 0x10U)
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
{ 
  uint32_t prioritygroup = 0x00U;
  
  /* Check the parameters */
  assert(IS_NVIC_SUB_PRIORITY(SubPriority));
  assert(IS_NVIC_PREEMPTION_PRIORITY(PreemptPriority));
  
  prioritygroup = NVIC_GetPriorityGrouping();
  
  NVIC_SetPriority(IRQn, NVIC_EncodePriority(prioritygroup, PreemptPriority, SubPriority));
}

void pendsv_init(void) {
    // set PendSV interrupt at lowest priority
    HAL_NVIC_SetPriority(PendSV_IRQn, IRQ_PRI_PENDSV, IRQ_SUBPRI_PENDSV);
	HAL_NVIC_SetPriority(MPPORT_SIGNAL_IRQn, IRQ_PRI_PENDSV - 1, IRQ_SUBPRI_PENDSV);
	NVIC_EnableIRQ(MPPORT_SIGNAL_IRQn);
}

// Call this function to raise a pending exception during an interrupt.
// It will first try to raise the exception "softly" by setting the
// mp_pending_exception variable and hoping that the VM will notice it.
// If this function is called a second time (ie with the mp_pending_exception
// variable already set) then it will force the exception by using the hardware
// PENDSV feature.  This will wait until all interrupts are finished then raise
// the given exception object using nlr_jump in the context of the top-level
// thread.
void pendsv_kbd_intr(void) {
    if (MP_STATE_VM(mp_pending_exception) == MP_OBJ_NULL) {
		MP_STATE_VM(mp_pending_exception) = MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_kbd_exception));
		#if MICROPY_ENABLE_SCHEDULER
		if (MP_STATE_VM(sched_state) == MP_SCHED_IDLE) {
			MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
		}
		#endif
    } else {
        MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
        pendsv_object = &MP_STATE_VM(mp_kbd_exception);
		MPPORT_SEND_SIGNAL(mpportsignal_longjmp);
    }
}

void pendsv_intr(void *pException) {
    if (MP_STATE_VM(mp_pending_exception) == MP_OBJ_NULL) {
		MP_STATE_VM(mp_pending_exception) = MP_OBJ_FROM_PTR(&pException);
		#if MICROPY_ENABLE_SCHEDULER
		if (MP_STATE_VM(sched_state) == MP_SCHED_IDLE) {
			MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
		}
		#endif
    } else {
        MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
        pendsv_object = &MP_STATE_VM(mp_kbd_exception);
        MPPORT_SEND_SIGNAL(mpportsignal_longjmp);
    }
}

// Call this function to raise a pending exception during an interrupt.
// It will first try to raise the exception "softly" by setting the
// mp_pending_exception variable and hoping that the VM will notice it.
// If this function is called a second time (ie with the mp_pending_exception
// variable already set) then it will force the exception by using the hardware
// PENDSV feature.  This will wait until all interrupts are finished then raise
// the given exception object using nlr_jump in the context of the top-level thread.
void pendsv_nlr_jump(void *o) {
    if (MP_STATE_VM(mp_pending_exception) == MP_OBJ_NULL) {
        MP_STATE_VM(mp_pending_exception) = o;
	    #if MICROPY_ENABLE_SCHEDULER
	    if (MP_STATE_VM(sched_state) == MP_SCHED_IDLE) {
	        MP_STATE_VM(sched_state) = MP_SCHED_PENDING;
	    }
	    #endif		
    } else {
        MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
        pendsv_object = o;
		sw_irq_count = 0;
		sw_count_ptr = &sw_irq_count;
		MPPORT_SEND_SIGNAL(mpportsignal_longjmp);
    }
}

// This will always force the exception by using the hardware PENDSV 
void pendsv_nlr_jump_hard(void *o) {
    MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
    pendsv_object = o;
	sw_irq_count = 0;
	sw_count_ptr = &sw_irq_count;
	MPPORT_SEND_SIGNAL(mpportsignal_longjmp);
}


#if defined(__CC_ARM)
__asm void NativeNLR(void) {
    // re-jig the stack so that when we return from this interrupt handler
    // it returns instead to nlr_jump with argument pendsv_object
    // note that stack has a different layout if DEBUG is enabled
    //
    // on entry to this (naked) function, stack has the following layout:
    //
    // stack layout with DEBUG disabled:
    //   sp[6]: pc=r15
    //   sp[5]: lr=r14
    //   sp[4]: r12
    //   sp[3]: r3
    //   sp[2]: r2
    //   sp[1]: r1
    //   sp[0]: r0
    //
    // stack layout with DEBUG enabled:
    //   sp[8]: pc=r15
    //   sp[7]: lr=r14
    //   sp[6]: r12
    //   sp[5]: r3
    //   sp[4]: r2
    //   sp[3]: r1
    //   sp[2]: r0
    //   sp[1]: 0xfffffff9
    //   sp[0]: ?
		IMPORT	pendsv_object
		IMPORT	nlr_jump
		IMPORT  sw_count_ptr
#if 1
		ldr r1,  =sw_count_ptr
		ldr r2,[r1]
		ldr r1,[r2]
		add r1, #1
		str r1, [r2]
		
		MRS     r1, psp
		//mov	r2,	#0x01000000
		//str r2, [r1, #28]
		
#if 0
        ldr r0, =pendsv_object
        ldr r0, [r0]
        cmp r0, 0
        beq no_obj
        str r0, [sp, #0]            // store to r0 on stack
        mov r0, #0
        str r0, [r1]                // clear pendsv_object
        ldr r0, =nlr_jump
        str r0, [sp, #24]           // store to pc on stack
        bx lr                       // return from interrupt; will return to nlr_jump

no_obj                    // pendsv_object==NULL
        push {r4-r11, lr}
        vpush {s16-s31}
        mrs r5, primask             // save PRIMASK in r5
        cpsid i                     // disable interrupts while we change stacks
        mov r0, sp                  // pass sp to save
        mov r4, lr                  // save lr because we are making a call
        bl pyb_thread_next          // get next thread to execute
        mov lr, r4                  // restore lr
        mov sp, r0                  // switch stacks
        msr primask, r5             // reenable interrupts
        vpop {s16-s31}
        pop {r4-r11, lr}
        bx lr                       // return from interrupt; will return to new thread

#else
		
        ldr r0, =pendsv_object
        ldr r0, [r0]
		
#if defined(PENDSV_DEBUG)
        str r0, [sp, #8]
#else
        str r0, [r1, #0]
#endif
        ldr r0, =nlr_jump
#if defined(PENDSV_DEBUG)
        str r0, [sp, #32]
#else
        str r0, [r1, #24]
#endif
        bx lr
#endif
#else
		mov	r2,	#0x01000000
		str r2, [sp, #28]    // reset XPSR, as pendSV may interrupt LDM/STM instructions who saves progress to XPSR

#if 0//MICROPY_PY_THREAD
        ldr r0, =pendsv_object
        ldr r0, [r0]
        cmp r0, 0
        beq no_obj
        str r0, [sp, #0]            // store to r0 on stack
        mov r0, #0
        str r0, [r1]                // clear pendsv_object
        ldr r0, =nlr_jump
        str r0, [sp, #24]           // store to pc on stack
        bx lr                       // return from interrupt; will return to nlr_jump

no_obj                    // pendsv_object==NULL
        push {r4-r11, lr}
        vpush {s16-s31}
        mrs r5, primask             // save PRIMASK in r5
        cpsid i                     // disable interrupts while we change stacks
        mov r0, sp                  // pass sp to save
        mov r4, lr                  // save lr because we are making a call
        bl pyb_thread_next          // get next thread to execute
        mov lr, r4                  // restore lr
        mov sp, r0                  // switch stacks
        msr primask, r5             // reenable interrupts
        vpop {s16-s31}
        pop {r4-r11, lr}
        bx lr                       // return from interrupt; will return to new thread

#else
        ldr r0, =pendsv_object
        ldr r0, [r0]
		
#if defined(PENDSV_DEBUG)
        str r0, [sp, #8]
#else
        str r0, [sp, #0]
#endif
        ldr r0, =nlr_jump
#if defined(PENDSV_DEBUG)
        str r0, [sp, #32]
#else
        str r0, [sp, #24]
#endif
        bx lr
#endif
#endif
    /*
    uint32_t x[2] = {0x424242, 0xdeaddead};
    printf("PendSV: %p\n", x);
    for (uint32_t *p = (uint32_t*)(((uint32_t)x - 15) & 0xfffffff0), i = 64; i > 0; p += 4, i -= 4) {
        printf(" %p: %08x %08x %08x %08x\n", p, (uint)p[0], (uint)p[1], (uint)p[2], (uint)p[3]);
    }
    */
}
#elif defined(__CLANG_ARM)
void NativeNLR(void) {
    // re-jig the stack so that when we return from this interrupt handler
    // it returns instead to nlr_jump with argument pendsv_object
    // note that stack has a different layout if DEBUG is enabled
    //
    // on entry to this (naked) function, stack has the following layout:
    //
    // stack layout with DEBUG disabled:
    //   sp[6]: pc=r15
    //   sp[5]: lr=r14
    //   sp[4]: r12
    //   sp[3]: r3
    //   sp[2]: r2
    //   sp[1]: r1
    //   sp[0]: r0
    //
    // stack layout with DEBUG enabled:
    //   sp[8]: pc=r15
    //   sp[7]: lr=r14
    //   sp[6]: r12
    //   sp[5]: r3
    //   sp[4]: r2
    //   sp[3]: r1
    //   sp[2]: r0
    //   sp[1]: 0xfffffff9
    //   sp[0]: ?
//		__asm("IMPORT	pendsv_object");
//		__asm("IMPORT	nlr_jump");
//		__asm("IMPORT  sw_count_ptr");
		
		
		__asm("ldr r1,  =sw_count_ptr");
		__asm("ldr r2,[r1]");
		__asm("ldr r1,[r2]");
		__asm("add r1, #1");
		__asm("str r1, [r2]");
		
		__asm("MRS     r1, psp");
        __asm("ldr r0, =pendsv_object");
        __asm("ldr r0, [r0]");
        __asm("str r0, [r1, #0]");
        __asm("ldr r0, =nlr_jump");
        __asm("str r0, [r1, #24]");

        __asm("bx lr");
}
#elif defined (__ICCARM__)
	// implemented in pendsv_iar.S
#else
__attribute__((naked)) void NativeNLR(void) {
    // re-jig the stack so that when we return from this interrupt handler
    // it returns instead to nlr_jump with argument pendsv_object
    // note that stack has a different layout if DEBUG is enabled
    //
    // on entry to this (naked) function, stack has the following layout:
    //
    // stack layout with DEBUG disabled:
    //   sp[6]: pc=r15
    //   sp[5]: lr=r14
    //   sp[4]: r12
    //   sp[3]: r3
    //   sp[2]: r2
    //   sp[1]: r1
    //   sp[0]: r0
    //
    // stack layout with DEBUG enabled:
    //   sp[8]: pc=r15
    //   sp[7]: lr=r14
    //   sp[6]: r12
    //   sp[5]: r3
    //   sp[4]: r2
    //   sp[3]: r1
    //   sp[2]: r0
    //   sp[1]: 0xfffffff9
    //   sp[0]: ?

#if MICROPY_PY_THREAD
    __asm volatile (
		"mov r2,	#0x01000000 \n"
		"str r2, [sp, #28]      \n"    // modify stacked XPSR to make sure possible LDM/STM progress is cleared
        "ldr r1, pendsv_object_ptr\n"
        "ldr r0, [r1]\n"
        "cmp r0, 0\n"
        "beq .no_obj\n"
        "str r0, [sp, #0]\n"            // store to r0 on stack
        "mov r0, #0\n"
        "str r0, [r1]\n"                // clear pendsv_object
        "ldr r0, nlr_jump_ptr\n"
        "str r0, [sp, #24]\n"           // store to pc on stack
        "bx lr\n"                       // return from interrupt; will return to nlr_jump

        ".no_obj:\n"                    // pendsv_object==NULL
        "push {r4-r11, lr}\n"
        "vpush {s16-s31}\n"
        "mrs r5, primask\n"             // save PRIMASK in r5
        "cpsid i\n"                     // disable interrupts while we change stacks
        "mov r0, sp\n"                  // pass sp to save
        "mov r4, lr\n"                  // save lr because we are making a call
        "bl pyb_thread_next\n"          // get next thread to execute
        "mov lr, r4\n"                  // restore lr
        "mov sp, r0\n"                  // switch stacks
        "msr primask, r5\n"             // reenable interrupts
        "vpop {s16-s31}\n"
        "pop {r4-r11, lr}\n"
        "bx lr\n"                       // return from interrupt; will return to new thread
        ".align 2\n"
        "pendsv_object_ptr: .word pendsv_object\n"
        "nlr_jump_ptr: .word nlr_jump\n"
    );
#else
    __asm volatile (
        "ldr r0, pendsv_object_ptr\n"
        "ldr r0, [r0]\n"
#if defined(PENDSV_DEBUG)
        "str r0, [sp, #8]\n"
#else
        "str r0, [sp, #0]\n"
#endif
        "ldr r0, nlr_jump_ptr\n"
#if defined(PENDSV_DEBUG)
        "str r0, [sp, #32]\n"
#else
        "str r0, [sp, #24]\n"
#endif
        "bx lr\n"
        ".align 2\n"
        "pendsv_object_ptr: .word pendsv_object\n"
        "nlr_jump_ptr: .word nlr_jump\n"
    );
#endif

    /*
    uint32_t x[2] = {0x424242, 0xdeaddead};
    printf("PendSV: %p\n", x);
    for (uint32_t *p = (uint32_t*)(((uint32_t)x - 15) & 0xfffffff0), i = 64; i > 0; p += 4, i -= 4) {
        printf(" %p: %08x %08x %08x %08x\n", p, (uint)p[0], (uint)p[1], (uint)p[2], (uint)p[3]);
    }
    */
}
#endif	// #ifdef __CC_ARM

typedef void(*pfnIrqHandler_t)(void);
extern void SysTick_Handler(void);
const pfnIrqHandler_t cs_signalMap[] = {
	SysTick_Handler,
	NativeNLR,
};

#if defined(__CC_ARM)
// "Borrow" this reserved IRQ slot to dispatch nlr_jump and other async signals
__asm void MPPORT_SIGNAL_HANDLER(void) /* naked */
{
	IMPORT s_mpySignalCode
	IMPORT cs_signalMap
	LDR   R0, =s_mpySignalCode
	LDR   R1, =cs_signalMap
	LDR   R0,  [R0]
	LDR   R1, [R1, R0, LSL #2]
	BX	  R1
}
#elif  defined(__CLANG_ARM)
void MPPORT_SIGNAL_HANDLER(void) /* naked */
{
//	__asm("IMPORT s_mpySignalCode");
//	__asm("IMPORT cs_signalMap");
	__asm("LDR   R0, =s_mpySignalCode");
	__asm("LDR   R1, =cs_signalMap");
	__asm("LDR   R0,  [R0]");
	__asm("LDR   R1, [R1, R0, LSL #2]");
	__asm("BX	  R1");
}
#endif
