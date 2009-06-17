/*-------------------------------------------------------------------------
 *  vmadump_x86.h:  Definitions for VMADump, shared by i386 and x86_64
 *
 *  Copyright (C) 1999-2001 by Erik Hendriks <erik@hendriks.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: vmadump_x86.h,v 1.9 2008/12/06 00:50:28 phargrov Exp $
 *
 *  THIS FILE ADDED FOR BLCR <http://ftg.lbl.gov/checkpoint>
 *-----------------------------------------------------------------------*/
#ifndef _VMADUMP_X86_H
#define _VMADUMP_X86_H

#include <asm/desc.h>
#include <asm/i387.h>

/* set_used_math() first appears in 2.6.11 */
#ifndef set_used_math
  #define used_math()		(current->used_math)
  #define clear_used_math()	do { current->used_math = 0; } while (0)
  #define set_used_math()	do { current->used_math = 1; } while (0)
#endif

/* The merge of x86 architectures yields thread.xstate in 2.6.26 and up */
#if HAVE_THREAD_I387
  typedef union i387_union vmad_i387_t;
  #define vmad_task_i387(_task) (&(_task)->thread.i387)
#elif HAVE_THREAD_XSTATE
  typedef union thread_xstate vmad_i387_t;
  #define vmad_task_i387(_task) ((_task)->thread.xstate)
#else
  #error "Unknown i387 state type"
#endif

/* Space for all the temporaries used in vmadump_restore_cpu().
 * They are too big to safely sit on the stack.
 */
struct vmadump_restore_tmps {
    struct pt_regs            _regtmp;
    union { /* Never live at the same time */
        struct thread_struct  _threadtmp;
        vmad_i387_t           _i387tmp;
    } _u;
};
#define VMAD_REGTMP(_x86tmp) \
    (&((_x86tmp)->_regtmp))
#define VMAD_THREADTMP(_x86tmp) \
    (&((_x86tmp)->_u._threadtmp))
#define VMAD_I387TMP(_x86tmp) \
    (&((_x86tmp)->_u._i387tmp))

static
long vmadump_store_i387(cr_chkpt_proc_req_t *ctx, struct file *file) {
    vmad_i387_t i387tmp;
    long r, bytes = 0;
    char flag;

    flag = !!used_math();
    r = write_kern(ctx, file, &flag, sizeof(flag));
    if (r != sizeof(flag)) goto err;
    bytes += r;

    if (flag) {
        kernel_fpu_begin();
        memcpy(&i387tmp, vmad_task_i387(current), sizeof(i387tmp));
        kernel_fpu_end();

        r = write_kern(ctx, file, &i387tmp, sizeof(i387tmp));
        if (r != sizeof(i387tmp)) goto err;
        bytes += r;
    }

    return bytes;

 err:
    if (r >= 0) r = -EIO;
    return r;
}

static inline int
vmad_check_fpu_state(void)
{
#if HAVE_RESTORE_FPU_CHECKING
  #if HAVE_2_6_0_RESTORE_FPU_CHECKING
    return restore_fpu_checking((struct i387_fxsave_struct *)vmad_task_i387(current));
  #elif HAVE_2_6_28_RESTORE_FPU_CHECKING
    return restore_fpu_checking(current);
  #else
    #error "Don't know how to call restore_fpu_checking"
  #endif
#else
    vmad_i387_t *i387tmp = vmad_task_i387(current);
    int r = 0;

    /* Invalid FPU states can blow us out of the water so we will do
     * the restore here in such a way that we trap the fault if the
     * restore fails.  This modeled after get_user and put_user. */
    if (cpu_has_fxsr) {
        asm volatile
        ("1: fxrstor %1               \n"
         "2:                          \n"
         ".section .fixup,\"ax\"      \n"
         "3:  movl %2, %0             \n"
         "    jmp 2b                  \n"
         ".previous                   \n"
         ".section __ex_table,\"a\"   \n"
         "    .align 4                \n"
         "    .long 1b, 3b            \n"
         ".previous                   \n"
         : "+r"(r)
         : "m" (i387tmp->fxsave), "i"(-EFAULT));
    } else {
        asm volatile
        ("1: frstor %1                \n"
         "2:                          \n"
         ".section .fixup,\"ax\"      \n"
         "3:  movl %2, %0             \n"
         "    jmp 2b                  \n"
         ".previous                   \n"
         ".section __ex_table,\"a\"   \n"
         "    .align 4                \n"
         "    .long 1b, 3b            \n"
         ".previous                   \n"
         : "+r"(r)
         : "m" (i387tmp->fsave), "i"(-EFAULT));
    }

    return r;
#endif /* !HAVE_RESTORE_FPU_CHECKING */
}

static
int vmadump_restore_i387(cr_rstrt_proc_req_t *ctx, struct file *file,
                         vmad_i387_t *i387tmp) {
#if !defined(CR_KDATA_xstate_size)
    const unsigned int xstate_size = sizeof(vmad_i387_t);
#endif
    char flag;
    int r;

    r = read_kern(ctx, file, &flag, sizeof(flag));
    if (r != sizeof(flag)) goto bad_read;

    if (flag) {
        r = -ENOMEM;
#if HAVE_THREAD_XSTATE
        /* Lazy allocation of FP state storage */
        if (!vmad_task_i387(current)) {
            init_fpu(current);
        }
#endif
        if (!vmad_task_i387(current)) {
            CR_ERR_CTX(ctx, "%d: FPU restore failure.", current->pid);
            goto bad_read;
        }
        r = read_kern(ctx, file, i387tmp, sizeof(*i387tmp));
        if (r != sizeof(*i387tmp)) {
            goto bad_read;
        }
    }

    /* Save the i387 state in thread_info and disable preemption
     * After kernel_fpu_begin(), we can ensure that
     * - TS_USEDFPU is clear
     * - TS is clear
     */
    kernel_fpu_begin();
    clear_used_math();

    if (flag) {
        /* NOTE: memcpy only xstate_size, which might be smaller than vmad_i387t */
        memcpy(vmad_task_i387(current), i387tmp, xstate_size);

        /* make sure the FPU state is good. */
        r = vmad_check_fpu_state();
        if (r) {
            CR_ERR_CTX(ctx, "%d: FPU restore failure %d.", current->pid, (int)r);
        } else {
            set_used_math();

            /* TS_USEDFPU will be set by math_state_restore() the next time
             * we FPU trap, so no need to set it here. 
             *
             * We only need to set current->used_math, so that 
             * math_state_restore() knows that the FPU state in
             * thread.{i387,xstate} is good. 
             */
        }
    } 

    /* kernel_fpu_end() should ensure TS is set */
    kernel_fpu_end();

    return 0;

 bad_read:
    if (r >= 0) r = -EIO;
    return r;
}

/* Save debugging state */
static
long vmadump_store_debugreg(cr_chkpt_proc_req_t *ctx, struct file *file) {
    long r, bytes = 0;

#if HAVE_THREAD_DEBUGREGS
    r = write_kern(ctx, file, &current->thread.debugreg,
		   sizeof(current->thread.debugreg));
    if (r != sizeof(current->thread.debugreg)) goto err;
    bytes += r;
#elif HAVE_THREAD_DEBUGREG0
    r = write_kern(ctx, file, &current->thread.debugreg0,
		   sizeof(current->thread.debugreg0));
    if (r != sizeof(current->thread.debugreg0)) goto err;
    bytes += r;
    r = write_kern(ctx, file, &current->thread.debugreg1,
		   sizeof(current->thread.debugreg1));
    if (r != sizeof(current->thread.debugreg1)) goto err;
    bytes += r;
    r = write_kern(ctx, file, &current->thread.debugreg2,
		   sizeof(current->thread.debugreg2));
    if (r != sizeof(current->thread.debugreg2)) goto err;
    bytes += r;
    r = write_kern(ctx, file, &current->thread.debugreg3,
		   sizeof(current->thread.debugreg3));
    if (r != sizeof(current->thread.debugreg3)) goto err;
    bytes += r;
    r = write_kern(ctx, file, &current->thread.debugreg6,
		   sizeof(current->thread.debugreg6));
    if (r != sizeof(current->thread.debugreg6)) goto err;
    bytes += r;
    r = write_kern(ctx, file, &current->thread.debugreg7,
		   sizeof(current->thread.debugreg7));
    if (r != sizeof(current->thread.debugreg7)) goto err;
    bytes += r;
#else
    #error
#endif

    return bytes;

 err:
    if (r >= 0) r = -EIO;
    return r;
}

/* Read (but don't restore) debugging state */
static
int vmadump_restore_debugreg(cr_rstrt_proc_req_t *ctx, struct file *file,
                             struct thread_struct *threadtmp) {
    int r;

#if HAVE_THREAD_DEBUGREGS
    r = read_kern(ctx, file, threadtmp->debugreg, sizeof(threadtmp->debugreg));
    if (r != sizeof(threadtmp->debugreg)) goto bad_read;
#elif HAVE_THREAD_DEBUGREG0
    r = read_kern(ctx, file, &threadtmp->debugreg0, sizeof(threadtmp->debugreg0));
    if (r != sizeof(threadtmp->debugreg0)) goto bad_read;
    r = read_kern(ctx, file, &threadtmp->debugreg1, sizeof(threadtmp->debugreg1));
    if (r != sizeof(threadtmp->debugreg1)) goto bad_read;
    r = read_kern(ctx, file, &threadtmp->debugreg2, sizeof(threadtmp->debugreg2));
    if (r != sizeof(threadtmp->debugreg2)) goto bad_read;
    r = read_kern(ctx, file, &threadtmp->debugreg3, sizeof(threadtmp->debugreg3));
    if (r != sizeof(threadtmp->debugreg3)) goto bad_read;
    r = read_kern(ctx, file, &threadtmp->debugreg6, sizeof(threadtmp->debugreg6));
    if (r != sizeof(threadtmp->debugreg6)) goto bad_read;
    r = read_kern(ctx, file, &threadtmp->debugreg7, sizeof(threadtmp->debugreg7));
    if (r != sizeof(threadtmp->debugreg7)) goto bad_read;
#else
    #error
#endif

    return 0;

 bad_read:
    if (r >= 0) r = -EIO;
    return r;
}

#endif /* _VMADUMP_X86_H */
