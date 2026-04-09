/**
 * @file    vuart.h
 * @brief   VirtualUART (VUART) — Public API Header
 *
 * @details Declares all public functions of the VirtualUART subsystem.
 *          This header is the sole external interface; all implementation
 *          details remain internal to vuart.c and the arch layer (VUart_Tc.c).
 *
 *          Target Hardware : Infineon AURIX TC499 (TriCore architecture)
 *          Hypervisor      : ImperioHV — Static Partitioning, Type-1 bare-metal
 *          Sponsor         : Siemens
 *
 *          Design Reference: VirtualUART Peripheral Feature — SWE1/SWE2/SWE3
 *
 * @note    MISRA-C:2012 compliance:
 *          - Every function parameter is const-qualified where the callee
 *            shall not modify the pointee (Rule 8.13).
 *          - All functions use the FUNC() macro (Rule 8.4 / AUTOSAR style).
 *          - No function-like macros with side effects (Rule 20.7).
 *
 * @version 1.0
 * @date    2025
 */

#ifndef VUART_H
#define VUART_H

/*===========================================================================*
 *  DEPENDENCIES                                                             *
 *===========================================================================*/
#include "vuart_types.h"

/*===========================================================================*
 *  AUTOSAR-STYLE FUNCTION DECLARATION MACRO                                *
 *  Expands to: <return_type> <name>                                        *
 *  The VUART_CODE memory section attribute is resolved by the linker        *
 *  configuration; for portability it is defined as empty here.             *
 *===========================================================================*/
#ifndef FUNC
#define FUNC(rettype, memclass) rettype /* MISRA deviation: Rule 20.10 —   \
                                         * token-pasting not used; macro    \
                                         * kept for AUTOSAR compatibility.  */
#endif

#ifndef VUART_CODE
#define VUART_CODE   /* memory section attribute — resolved by linker cfg */
#endif

/*===========================================================================*
 *  PUBLIC API — INITIALISATION                                             *
 *===========================================================================*/

/**
 * @brief   Initialise all VirtualUART structures, L2 MPU regions, and ISR
 *          routing on the calling core.
 *
 * @details This function must be called exactly once per core before the
 *          first VM is launched on that core.  It performs the following
 *          actions:
 *          -# Zeroes every VUart_InstanceType instance in cfg->instances[].
 *          -# Programs the L2 MPU trap region for each VM via the arch-layer
 *             stub VUart_Tc_ConfigMpuRegion().
 *          -# Routes the physical UART RX interrupt to the HV context.
 *          -# Zeroes the shared TX queue slots and IPC channels; initialises
 *             their spinlocks.
 *
 * @param[in] cfg  Pointer to the compile-time feature configuration for this
 *                 core.  Must not be NULL; lifetime must span the entire
 *                 hypervisor run.
 *
 * @return  void
 *
 * @pre     Called before VUart_TrapHandler() or VUart_RxIsr() can fire.
 * @post    Every VM in cfg->instances[] has state == VUART_STATE_IDLE and its
 *          MPU trap region is active.
 *
 * Requirements: [SWS_VUART_00002] [SWS_VUART_00003] [SWS_VUART_00004]
 *               [SWS_VUART_00005] [SWS_VUART_00501] [SWS_VUART_00502]
 */
FUNC(void, VUART_CODE) VUart_Init(const VUart_ConfigType *cfg);

/*===========================================================================*
 *  PUBLIC API — TRAP HANDLING (ENTRY FROM HV DISPATCHER)                  *
 *===========================================================================*/

/**
 * @brief   Primary entry point from the HV trap dispatcher for all L2MPR /
 *          L2MPW traps whose fault address falls within a virtual UART window.
 *
 * @details The hypervisor's top-level trap dispatcher calls this function
 *          when it detects HV Trap Class 2 (TIN 0 = L2MPR read trap,
 *          TIN 1 = L2MPW write trap) and the faulting address belongs to one
 *          of the registered virtual UART windows.
 *
 *          Internal operation:
 *          -# Derives vm_id     = (fault_addr >> 4) & 0x0F.
 *          -# Derives reg_offset = fault_addr & 0x0F.
 *          -# On write (ctx->TIN == 1): calls VUart_WriteReg().
 *          -# On read  (ctx->TIN == 0): calls VUart_ReadReg().
 *          -# Advances ctx->PC by VUART_INSTR_SIZE (4 bytes for a standard
 *             32-bit TriCore ST/LD instruction) so the VM resumes at the
 *             instruction following the faulting access.
 *
 * @param[in,out] ctx         Pointer to the saved CPU context.  PC, D[],
 *                             and A[] may be modified before trap return.
 * @param[in]     fault_addr  Virtual address that caused the trap, as
 *                             reported by the TriCore L2MPU fault register.
 *
 * @return  void
 *
 * @pre     ctx is a valid pointer to the fully saved HV_CpuContext captured
 *          by the HV entry stub.
 * @pre     VUart_Init() has been called on this core.
 * @post    ctx->PC advanced past the faulting instruction.
 * @post    For write traps: the appropriate virtual register action has been
 *          performed and (for DR writes) a frame has been enqueued.
 * @post    For read traps: the emulated register value has been injected into
 *          ctx->D[ctx->dst_reg].
 *
 * Requirements: [SWS_VUART_00003] [SWS_VUART_00101] [SWS_VUART_00102]
 *               [SWS_VUART_00103] [SWS_VUART_00104]
 */
FUNC(void, VUART_CODE) VUart_TrapHandler(TrapCtx_t *ctx, uint32_t fault_addr);

/*===========================================================================*
 *  PUBLIC API — VIRTUAL REGISTER EMULATION                                 *
 *===========================================================================*/

/**
 * @brief   Emulate a VM write to a virtual UART register.
 *
 * @details Called by VUart_TrapHandler() on an L2MPW (TIN 1) trap.
 *          Extracts the written value from the saved CPU context (ctx->D[src_reg])
 *          and dispatches to the correct register action:
 *
 *          | reg_offset | Register | Action                                      |
 *          |------------|----------|---------------------------------------------|
 *          | 0x00       | DR       | Build frame, enqueue to TX queue,           |
 *          |            |          | clear SR.TXRDY.                             |
 *          | 0x08       | CR       | Update CR shadow (writable bits only).      |
 *          | 0x0C       | DST_ID   | Update DST_ID; 0xFF enables broadcast.      |
 *          | other      | —        | Silent no-op.                               |
 *
 * @param[in,out] ctx        Saved CPU context. src_reg field identifies the
 *                            D-register holding the value written by the VM.
 * @param[in]     vm_id      VM identifier derived from fault_addr. Range 0..NUM_VMS-1.
 * @param[in]     reg_offset Register offset derived from fault_addr. Range 0x00..0x0F.
 *
 * @return  void
 *
 * @pre     vm_id < VUART_NUM_VMS.
 * @pre     VUart_Init() has been called.
 *
 * Requirements: [SWS_VUART_00102] [SWS_VUART_00101] [SWS_VUART_00201]
 *               [SWS_VUART_00202] [SWS_VUART_00203]
 */
FUNC(void, VUART_CODE) VUart_WriteReg(TrapCtx_t *ctx,
                                       uint8_t    vm_id,
                                       uint8_t    reg_offset);

/**
 * @brief   Emulate a VM read from a virtual UART register.
 *
 * @details Called by VUart_TrapHandler() on an L2MPR (TIN 0) trap.
 *          Injects the emulated register value into ctx->D[dst_reg] so the
 *          VM's load instruction receives the correct result after the trap
 *          return.
 *
 *          | reg_offset | Register | Action                                         |
 *          |------------|----------|------------------------------------------------|
 *          | 0x00       | DR       | Pop byte from rx_buf; clear RXRDY if empty;    |
 *          |            |          | inject into D[dst_reg].                        |
 *          | 0x04       | SR       | Inject current SR shadow into D[dst_reg].      |
 *          | 0x08       | CR       | Inject current CR shadow into D[dst_reg].      |
 *          | other      | —        | Inject 0x00 (safe default).                    |
 *
 * @param[in,out] ctx        Saved CPU context. dst_reg field identifies the
 *                            D-register that will receive the emulated value.
 * @param[in]     vm_id      VM identifier. Range 0..NUM_VMS-1.
 * @param[in]     reg_offset Register offset. Range 0x00..0x0F.
 *
 * @return  void
 *
 * @pre     vm_id < VUART_NUM_VMS.
 *
 * Requirements: [SWS_VUART_00103] [SWS_VUART_00101] [SWS_VUART_00303]
 */
FUNC(void, VUART_CODE) VUart_ReadReg(TrapCtx_t *ctx,
                                      uint8_t    vm_id,
                                      uint8_t    reg_offset);

/*===========================================================================*
 *  PUBLIC API — FRAME BUILDER                                               *
 *===========================================================================*/

/**
 * @brief   Construct a fully serialised VUART wire frame in f->raw[].
 *
 * @details Fills the frame fields and serialises them into f->raw[] with the
 *          following layout:
 *          @code
 *          raw[0]        = 0xAA  (SOF)
 *          raw[1]        = dst
 *          raw[2]        = src
 *          raw[3]        = reg
 *          raw[4]        = len
 *          raw[5..5+N-1] = data[0..N-1]
 *          raw[5+N]      = CRC8 (XOR of raw[0..5+N-1])
 *          @endcode
 *
 *          Sets f->raw_len = 6 + len. [SWS_VUART_00202] [SWS_VUART_00203]
 *
 * @param[out] f    Pointer to the frame structure to populate.
 * @param[in]  dst  Destination VM identifier (0..NUM_VMS-1 or 0xFF for broadcast).
 * @param[in]  src  Source VM identifier.
 * @param[in]  reg  Target virtual register offset (0x00, 0x04, 0x08).
 * @param[in]  data Pointer to the payload byte array. Must not be NULL.
 * @param[in]  len  Number of payload bytes (1..VUART_MAX_PAYLOAD).
 *
 * @return  void
 *
 * @pre     len >= 1U && len <= VUART_MAX_PAYLOAD.
 * @pre     data is a valid pointer to at least len bytes.
 * @post    f->raw[] contains the complete serialised frame.
 * @post    f->raw_len == 6U + len.
 *
 * Requirements: [SWS_VUART_00201] [SWS_VUART_00202] [SWS_VUART_00203]
 */
FUNC(void, VUART_CODE) VUart_BuildFrame(VUart_FrameType *f,
                                         uint8_t          dst,
                                         uint8_t          src,
                                         uint8_t          reg,
                                         const uint8_t   *data,
                                         uint8_t          len);

/*===========================================================================*
 *  PUBLIC API — RX ISR AND FRAME DISPATCH                                  *
 *===========================================================================*/

/**
 * @brief   Physical UART RX interrupt service routine (HV context).
 *
 * @details Reads one byte from the ASCLIN RX data register and feeds it to
 *          VUart_FrameParser_Feed().  This function is registered as the
 *          exclusive handler for the physical UART RX interrupt; no VM ISR
 *          handles this interrupt directly. [SWS_VUART_00005]
 *
 * @return  void
 *
 * @note    Interrupt context — no blocking, no spinlock acquisition here.
 *
 * Requirements: [SWS_VUART_00005] [SWS_VUART_00301]
 */
FUNC(void, VUART_CODE) VUart_RxIsr(void);

/**
 * @brief   Feed one received byte into the frame parser FSM.
 *
 * @details Advances the FSM one step. On a valid complete frame, calls
 *          VUart_DispatchFrame(). On CRC mismatch or invalid DST_VM_ID,
 *          silently resets to IDLE. [SWS_VUART_00301] [SWS_VUART_00302]
 *
 * @param[in,out] p     Pointer to the parser state. Must not be NULL.
 * @param[in]     byte  The received byte to process.
 *
 * @return  void
 *
 * Requirements: [SWS_VUART_00301] [SWS_VUART_00302] [SWS_VUART_00305]
 */
FUNC(void, VUART_CODE) VUart_FrameParser_Feed(VUart_FrameParserType *p,
                                               uint8_t                byte);

/**
 * @brief   Route a validated frame payload to the destination VM's RX buffer.
 *
 * @details Handles both same-core and cross-core delivery:
 *          - Same core: pushes payload bytes to the VM's rx_buf; sets
 *            SR.RXRDY; optionally injects a virtual interrupt if CR.RXIE=1.
 *          - Remote core: pushes the frame to the appropriate IPC ring under
 *            spinlock; sends an IPI via VUart_Tc_SendIPI(). [SWS_VUART_00402]
 *
 * @param[in] frame  Pointer to the fully validated frame. Must not be NULL.
 *
 * @return  void
 *
 * Requirements: [SWS_VUART_00303] [SWS_VUART_00304] [SWS_VUART_00401]
 *               [SWS_VUART_00402] [SWS_VUART_00403]
 */
FUNC(void, VUART_CODE) VUart_DispatchFrame(const VUart_FrameType *frame);

#endif /* VUART_H */
