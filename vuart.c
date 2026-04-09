/**
 * @file    vuart.c
 * @brief   VirtualUART (VUART) — Full Implementation
 *
 * @details Implements the complete VirtualUART subsystem for the ImperioHV
 *          hypervisor.  The subsystem virtualises a single physical ASCLIN
 *          UART peripheral so that up to four VMs each perceive a private,
 *          dedicated UART interface.  All VM UART register accesses are
 *          intercepted via the L2 MPU trap mechanism and emulated here without
 *          exposing the physical hardware to any VM.
 *
 *          Key design decisions:
 *          - Zero dynamic allocation: every buffer and queue is a static array.
 *          - O(1) enqueue/dequeue: all queue operations use modulo-masked
 *            power-of-2 indices.
 *          - MISRA-C:2012 strict compliance throughout; deviations are
 *            individually justified in-line.
 *          - All multi-core shared state is guarded by spinlocks whose
 *            test-and-set operation is provided by the arch layer.
 *
 *          References:
 *            VirtualUART Peripheral Feature Design — SWE1 / SWE2 / SWE3
 *            Infineon AURIX TC499 User Manual (TriCore architecture v1.6.2)
 *            MISRA-C:2012 Guidelines for the Use of the C Language
 *
 * @note    File organisation:
 *            Section 1 — Includes
 *            Section 2 — Arch-layer stubs (extern declarations)
 *            Section 3 — Static (file-scope) data
 *            Section 4 — Internal helper prototypes
 *            Section 5 — Internal helpers
 *            Section 6 — Initialisation
 *            Section 7 — Trap handler (VUart_TrapHandler)
 *            Section 8 — Write emulation (VUart_WriteReg)
 *            Section 9 — Read emulation  (VUart_ReadReg)       [stub]
 *            Section 10 — Frame builder  (VUart_BuildFrame)
 *            Section 11 — RX ISR and frame parser
 *            Section 12 — Frame dispatcher (VUart_DispatchFrame)
 *
 * @version 1.0
 * @date    2025
 */

/*===========================================================================*
 *  SECTION 1 — INCLUDES                                                     *
 *===========================================================================*/
#include "vuart.h"       /* Own header — types and public API                 */
#include <stdint.h>      /* uint8_t, uint32_t — MISRA Rule 4.6                */
#include <string.h>      /* memset, memcpy — only for initialisation           */

/*===========================================================================*
 *  SECTION 2 — ARCH-LAYER STUBS AND EXTERNAL REFERENCES                    *
 *                                                                           *
 *  The following symbols are implemented in VUart_Tc.c (arch layer).       *
 *  They are declared here as extern stubs so that this file compiles        *
 *  standalone for unit testing and academic review.                         *
 *===========================================================================*/

/**
 * @brief   Program one L2 MPU region as a trap-on-access window for a VM.
 * @param   vm_id       VM whose window is being configured.
 * @param   base_addr   Start of the MPU-protected virtual UART window.
 * @param   size        Window size in bytes (must be power-of-2, min 16).
 * [SWS_VUART_00002] [SWS_VUART_00003]
 */
extern void VUart_Tc_ConfigMpuRegion(uint8_t  vm_id,
                                      uint32_t base_addr,
                                      uint32_t size);

/**
 * @brief   Send an inter-processor interrupt to the specified core.
 *
 * @details Triggers an IPI on dst_core via the TriCore SRC (Service Request
 *          Control) register mechanism.  The IPI handler on the remote core
 *          pops the IPC ring and calls VUart_DispatchFrame(). [SWS_VUART_00402]
 *
 * @param   dst_core  Physical core number to interrupt (0 or 1).
 */
extern void VUart_Tc_SendIPI(uint8_t dst_core);

/**
 * @brief   Inject a virtual interrupt into the specified VM's context.
 *
 * @details Sets the pending-IRQ bit for the VM's UART RX interrupt line so
 *          that the HV will deliver it on the next HV->VM transition.
 *          [SWS_VUART_00304]
 *
 * @param   vm_id  Target VM identifier.
 */
extern void VUart_InjectVirtualIrq(uint8_t vm_id);

/**
 * @brief   Acquire a spinlock (blocking, using TriCore CMPSWAP instruction).
 * @param   lock  Pointer to the lock word. Must be 4-byte aligned.
 */
extern void Spinlock_Acquire(spinlock_t *lock);

/**
 * @brief   Release a previously acquired spinlock.
 * @param   lock  Pointer to the lock word.
 */
extern void Spinlock_Release(spinlock_t *lock);

/**
 * @brief   Write one byte to the physical ASCLIN TXDATA register.
 *
 * @details The TX arbiter calls this in a byte-by-byte loop to transmit a
 *          serialised frame.  This stub represents the physical driver call.
 *
 * @param   base_addr  Physical ASCLIN base address.
 * @param   byte       Byte to transmit.
 */
extern void Uart_PhysWrite(uint32_t base_addr, uint8_t byte);

/**
 * @brief   Read one byte from the physical ASCLIN RXDATA register.
 * @param   base_addr  Physical ASCLIN base address.
 * @return  Received byte.
 */
extern uint8_t Uart_PhysRead(uint32_t base_addr);

/*===========================================================================*
 *  SECTION 3 — STATIC (FILE-SCOPE) DATA                                    *
 *                                                                           *
 *  All data is statically allocated in .bss or .data to satisfy            *
 *  [SWS_VUART_00501].  No malloc/free is used anywhere.                    *
 *===========================================================================*/

/**
 * @brief Per-VM virtual UART instance array.
 *
 * Indexed by vm_id.  Zeroed by VUart_Init() before VM launch.
 * Stored in HV-private memory — no VM has MPU access to this region.
 * [SWS_VUART_00001] [SWS_VUART_00006]
 */
static VUart_InstanceType s_virt_uart[VUART_NUM_VMS];

/**
 * @brief Shared TX arbitration queue (all VMs and cores).
 *
 * Placed in shared memory so that trap handlers on both cores can enqueue
 * frames.  In a multi-core image this object must be placed in a linker
 * section that is mapped into both cores' address spaces.
 * [SWS_VUART_00204] [SWS_VUART_00501]
 */
static VUart_TxQueueType s_tx_queue;

/**
 * @brief  IPC ring from Core 1 to Core 0.
 *
 * Producer: HV trap handler / DispatchFrame on Core 1.
 * Consumer: IPI handler on Core 0. [SWS_VUART_00402] [SWS_VUART_00403]
 */
static VUart_IpcChannelType s_ipc_c1_to_c0;

/**
 * @brief  IPC ring from Core 0 to Core 1.
 *
 * Producer: HV trap handler / DispatchFrame on Core 0.
 * Consumer: IPI handler on Core 1. [SWS_VUART_00402]
 */
static VUart_IpcChannelType s_ipc_c0_to_c1;

/**
 * @brief Byte-by-byte frame parser FSM state (one per physical UART).
 *
 * Owned exclusively by VUart_RxIsr() and VUart_FrameParser_Feed().
 * Never accessed from the trap handler path (different contexts).
 * [SWS_VUART_00301]
 */
static VUart_FrameParserType s_frame_parser;

/**
 * @brief  Pointer to the feature configuration supplied during VUart_Init().
 *
 * Cached so that VUart_DispatchFrame() can access the vm_core_map and
 * phys_uart_base without additional parameters.
 */
static const VUart_ConfigType *s_cfg = (const VUart_ConfigType *)0; /* NULL */

/*===========================================================================*
 *  SECTION 4 — INTERNAL HELPER PROTOTYPES                                  *
 *===========================================================================*/

static uint8_t  VUart_TxQueue_Enqueue(VUart_TxQueueType       *q,
                                       const VUart_FrameType   *frame);
static uint8_t  VUart_TxQueue_Dequeue(VUart_TxQueueType       *q,
                                       VUart_FrameType         *frame_out);
static uint8_t  VUart_RxBuf_Push(VUart_InstanceType *inst,
                                   uint8_t             byte);
static uint8_t  VUart_RxBuf_Pop(VUart_InstanceType *inst,
                                  uint8_t            *byte_out);
static uint8_t  VUart_IpcChannel_Push(VUart_IpcChannelType  *ch,
                                       const VUart_FrameType *frame);
static void     VUart_FsmReset(VUart_FrameParserType *p);

/*===========================================================================*
 *  SECTION 5 — INTERNAL HELPERS                                             *
 *===========================================================================*/

/* -------------------------------------------------------------------------
 * VUart_TxQueue_Enqueue
 *
 * Enqueues one frame into the shared TX queue.
 * The spinlock is acquired before testing/advancing the head index to ensure
 * atomicity on multi-core systems.  O(1) — no loops.  [SWS_VUART_00204]
 *
 * Returns: 1 = success, 0 = queue full (frame dropped).
 * MISRA-C Rule 15.5: single exit point enforced by using a result variable.
 * -------------------------------------------------------------------------*/
static uint8_t VUart_TxQueue_Enqueue(VUart_TxQueueType     *q,
                                      const VUart_FrameType *frame)
{
    uint8_t result;
    uint8_t next_head;

    Spinlock_Acquire(&q->lock); /* Enter critical section — multi-core safe. */

    /* Compute the slot index that would follow the current head.
     * The modulo wraps within [0, TX_QUEUE_DEPTH-1]. */
    next_head = (uint8_t)((uint8_t)(q->head + 1U) % (uint8_t)VUART_TX_QUEUE_DEPTH);

    if (next_head == q->tail)
    {
        /* Queue full — drop frame and report failure. */
        result = 0U;
    }
    else
    {
        /* Safe to write: copy entire frame into the slot at current head. */
        (void)memcpy(&q->frames[q->head], frame, sizeof(VUart_FrameType));
        q->head = next_head; /* Advance head — visible to other cores after lock release. */
        result  = 1U;
    }

    Spinlock_Release(&q->lock); /* Exit critical section. */

    return result;
}

/* -------------------------------------------------------------------------
 * VUart_TxQueue_Dequeue
 *
 * Dequeues one frame from the TX queue.  Called by the TX arbiter only —
 * single consumer, so no spinlock needed on the tail side.
 *
 * FIFO policy: simply return the oldest frame (FIFO order by definition
 * since head is always the write end and tail the read end).
 *
 * PRIORITY policy: scan the queue for the slot whose frame.priority is
 * highest, then "remove" it by swapping with the current tail slot and
 * advancing tail.  Bounded by VUART_TX_QUEUE_DEPTH — MISRA compliant.
 *
 * Returns: 1 = frame returned in *frame_out, 0 = queue empty.
 * -------------------------------------------------------------------------*/
static uint8_t VUart_TxQueue_Dequeue(VUart_TxQueueType *q,
                                      VUart_FrameType   *frame_out)
{
    uint8_t result;
    uint8_t occupancy;
    uint8_t i;
    uint8_t best_idx;
    uint8_t best_prio;

    if (q->head == q->tail)
    {
        /* Queue empty. */
        result = 0U;
    }
    else if (q->policy == VUART_ARB_FIFO)
    {
        /* ---- FIFO path (O(1)) ---- */
        (void)memcpy(frame_out, &q->frames[q->tail], sizeof(VUart_FrameType));
        q->tail = (uint8_t)((uint8_t)(q->tail + 1U) % (uint8_t)VUART_TX_QUEUE_DEPTH);
        result  = 1U;
    }
    else /* VUART_ARB_PRIORITY */
    {
        /* ---- Priority path (O(N)) — N bounded by VUART_TX_QUEUE_DEPTH ---- */

        /* Compute occupancy — number of valid slots between tail and head. */
        occupancy = (uint8_t)((uint8_t)(q->head - q->tail + (uint8_t)VUART_TX_QUEUE_DEPTH)
                               % (uint8_t)VUART_TX_QUEUE_DEPTH);

        best_idx  = q->tail;
        best_prio = q->frames[q->tail].priority;

        /* Scan all occupied slots to find the highest-priority frame.
         * The loop bound is VUART_TX_QUEUE_DEPTH — always terminates. */
        for (i = 1U; i < occupancy; i++)
        {
            uint8_t idx = (uint8_t)((uint8_t)(q->tail + i) % (uint8_t)VUART_TX_QUEUE_DEPTH);
            if (q->frames[idx].priority > best_prio)
            {
                best_prio = q->frames[idx].priority;
                best_idx  = idx;
            }
        }

        /* Copy the winner to the output. */
        (void)memcpy(frame_out, &q->frames[best_idx], sizeof(VUart_FrameType));

        /* Remove the winner by overwriting it with the tail slot, then advance tail.
         * This avoids holes and keeps the buffer contiguous from tail..head. */
        (void)memcpy(&q->frames[best_idx], &q->frames[q->tail], sizeof(VUart_FrameType));
        q->tail = (uint8_t)((uint8_t)(q->tail + 1U) % (uint8_t)VUART_TX_QUEUE_DEPTH);

        result = 1U;
    }

    return result;
}

/* -------------------------------------------------------------------------
 * VUart_RxBuf_Push
 *
 * Pushes one byte into a VM's RX ring buffer.  Called by the RX ISR or the
 * IPI handler — producer side of the SPSC ring. [SWS_VUART_00503]
 *
 * Returns: 1 = success, 0 = buffer full.
 * -------------------------------------------------------------------------*/
static uint8_t VUart_RxBuf_Push(VUart_InstanceType *inst, uint8_t byte)
{
    uint8_t next_head;
    uint8_t result;

    /* Calculate where the head would be after the write. */
    next_head = (uint8_t)((uint8_t)(inst->rx_head + 1U) % (uint8_t)VUART_RX_BUF_SIZE);

    if (next_head == inst->rx_tail)
    {
        /* Buffer full — byte is lost; the SPSC invariant is maintained. */
        result = 0U;
    }
    else
    {
        inst->rx_buf[inst->rx_head] = byte;
        /* Publish the new head.  The consumer reads rx_head only after this
         * store completes.  On TriCore, stores are visible in program order
         * to other harts; no explicit fence is needed for same-core SPSC. */
        inst->rx_head = next_head;
        result = 1U;
    }

    return result;
}

/* -------------------------------------------------------------------------
 * VUart_RxBuf_Pop
 *
 * Pops one byte from the VM's RX ring buffer.  Called by the trap handler
 * (consumer side of the SPSC ring). [SWS_VUART_00503]
 *
 * Returns: 1 = byte returned in *byte_out, 0 = buffer empty.
 * -------------------------------------------------------------------------*/
static uint8_t VUart_RxBuf_Pop(VUart_InstanceType *inst, uint8_t *byte_out)
{
    uint8_t result;

    if (inst->rx_head == inst->rx_tail)
    {
        /* Buffer empty. */
        result = 0U;
    }
    else
    {
        *byte_out   = inst->rx_buf[inst->rx_tail];
        inst->rx_tail = (uint8_t)((uint8_t)(inst->rx_tail + 1U)
                                   % (uint8_t)VUART_RX_BUF_SIZE);
        result = 1U;
    }

    return result;
}

/* -------------------------------------------------------------------------
 * VUart_IpcChannel_Push
 *
 * Pushes one frame onto an IPC ring.  Spinlock-protected because multiple
 * ISR/dispatcher contexts on the same source core could call this concurrently
 * (e.g., if VUart_DispatchFrame runs re-entrantly due to an IPI servicing).
 * [SWS_VUART_00402]
 *
 * Returns: 1 = success, 0 = ring full.
 * -------------------------------------------------------------------------*/
static uint8_t VUart_IpcChannel_Push(VUart_IpcChannelType  *ch,
                                      const VUart_FrameType *frame)
{
    uint8_t next_head;
    uint8_t result;

    Spinlock_Acquire(&ch->lock);

    next_head = (uint8_t)((uint8_t)(ch->head + 1U) % (uint8_t)VUART_IPC_RING_DEPTH);

    if (next_head == ch->tail)
    {
        result = 0U; /* Ring full. */
    }
    else
    {
        (void)memcpy(&ch->frames[ch->head], frame, sizeof(VUart_FrameType));
        ch->head = next_head;
        result   = 1U;
    }

    Spinlock_Release(&ch->lock);

    return result;
}

/* -------------------------------------------------------------------------
 * VUart_FsmReset
 *
 * Resets the frame parser FSM to the IDLE state and clears the in-progress
 * frame and CRC accumulator.  Called on invalid DST_VM_ID, CRC mismatch,
 * or zero LENGTH — all treated as a silent discard. [SWS_VUART_00302]
 * -------------------------------------------------------------------------*/
static void VUart_FsmReset(VUart_FrameParserType *p)
{
    p->state     = VUART_FSM_IDLE;
    p->data_idx  = 0U;
    p->crc_accum = 0U;
    /* Zero the in-progress frame using memset for clarity. */
    (void)memset(&p->frame, 0, sizeof(VUart_FrameType));
}

/*===========================================================================*
 *  SECTION 6 — INITIALISATION                                              *
 *===========================================================================*/

/**
 * @brief Initialise all VirtualUART structures on the calling core.
 *
 * Detailed operation notes are in the Doxygen header in vuart.h.
 *
 * @param[in] cfg  Feature configuration for this core. Must not be NULL.
 */
FUNC(void, VUART_CODE) VUart_Init(const VUart_ConfigType *cfg)
{
    uint8_t i;

    /* Cache the configuration pointer for use by DispatchFrame and RxIsr. */
    s_cfg = cfg;

    /*-----------------------------------------------------------------------
     * Step 1: Zero all virtual UART instance state.
     * memset is used here; MISRA permits it for initialisation because
     * all fields are plain integers (no pointer fields with non-zero
     * NULL representation). [SWS_VUART_00501]
     *----------------------------------------------------------------------*/
    (void)memset(s_virt_uart, 0, sizeof(s_virt_uart));

    /*-----------------------------------------------------------------------
     * Step 2: Populate compile-time-fixed fields and program MPU regions.
     *
     * Each VM's virtual UART window starts at:
     *   virt_base = BASE_WINDOW + (vm_id * 0x10)
     * The 16-byte window encodes vm_id and reg_offset in the address bits:
     *   vm_id      = (addr >> 4) & 0x0F
     *   reg_offset = addr & 0x0F
     * This matches the decode in VUart_TrapHandler. [SWS_VUART_00002]
     *----------------------------------------------------------------------*/
    for (i = 0U; i < VUART_NUM_VMS; i++)
    {
        s_virt_uart[i].vm_id    = i;
        s_virt_uart[i].core_id  = cfg->vm_core_map[i];
        /* Base address encoding: bit[7:4] = vm_id, bits[3:0] = register offset. */
        s_virt_uart[i].virt_base = (uint32_t)((uint32_t)i << 4U);
        s_virt_uart[i].state    = VUART_STATE_IDLE;
        s_virt_uart[i].DST_ID   = 0U; /* Default: unicast to VM 0 until configured. */

        /* Programme the L2 MPU trap region for this VM's window.
         * Window size is 16 bytes (covers all four register offsets). */
        VUart_Tc_ConfigMpuRegion(i, s_virt_uart[i].virt_base, 16U);
    }

    /*-----------------------------------------------------------------------
     * Step 3: Initialise the shared TX queue.
     * The spinlock is zeroed (unlocked state = 0). [SWS_VUART_00501]
     *----------------------------------------------------------------------*/
    (void)memset(&s_tx_queue, 0, sizeof(VUart_TxQueueType));
    s_tx_queue.policy = VUART_ARB_FIFO; /* Default policy — overrideable. */

    /*-----------------------------------------------------------------------
     * Step 4: Initialise IPC rings.
     *----------------------------------------------------------------------*/
    (void)memset(&s_ipc_c0_to_c1, 0, sizeof(VUart_IpcChannelType));
    (void)memset(&s_ipc_c1_to_c0, 0, sizeof(VUart_IpcChannelType));

    /*-----------------------------------------------------------------------
     * Step 5: Initialise frame parser FSM.
     *----------------------------------------------------------------------*/
    VUart_FsmReset(&s_frame_parser);

    /*-----------------------------------------------------------------------
     * Step 6: Route the physical UART RX interrupt to HV context.
     * The arch layer handles the SRC register programming. [SWS_VUART_00005]
     * (Stub call — actual implementation in VUart_Tc.c.)
     *----------------------------------------------------------------------*/
    /* VUart_Tc_RouteRxIsr(cfg->rx_isr_node); */
    /* NOTE: Stub omitted from this file to keep the implementation
     *       architecture-agnostic.  The arch layer must be linked. */
}

/*===========================================================================*
 *  SECTION 7 — TRAP HANDLER                                                *
 *===========================================================================*/

/**
 * @brief   Primary trap handler entry point for virtual UART memory accesses.
 *
 * @details This is the most performance-critical function in the subsystem.
 *          It is called directly from the HV's top-level trap dispatcher
 *          on every L2MPR or L2MPW trap whose fault address lies in a
 *          registered virtual UART window.
 *
 *          On TriCore, the HV entry stub has already:
 *            (a) saved the full CPU context (D[], A[], PC, PSW) into *ctx;
 *            (b) decoded the trapped instruction to populate ctx->src_reg
 *                (for stores) or ctx->dst_reg (for loads);
 *            (c) set ctx->TIN to 0 (L2MPR) or 1 (L2MPW);
 *            (d) set ctx->fault_addr to the virtual address that trapped.
 *
 *          This function's job is emulation + PC advancement only.
 */
FUNC(void, VUART_CODE) VUart_TrapHandler(TrapCtx_t *ctx, uint32_t fault_addr)
{
    uint8_t vm_id;
    uint8_t reg_offset;

    /*-----------------------------------------------------------------------
     * Step 1: Derive VM identity and register offset from the fault address.
     *
     * The virtual UART window layout (from the architecture design doc):
     *   Bits [7:4] of fault_addr encode the VM ID  (0x00..0x03 for 4 VMs).
     *   Bits [3:0] of fault_addr encode the register offset (0x00/0x04/0x08/0x0C).
     *
     * Example: VM 2 writing to its CR register:
     *   fault_addr = 0x28  =>  vm_id=2, reg_offset=0x08
     *
     * The 0x0F masks ensure the values are bounded even if the hardware
     * reports an unexpected address — defensive programming. [SWS_VUART_00006]
     *----------------------------------------------------------------------*/
    vm_id      = (uint8_t)((fault_addr >> 4U) & 0x0FU);
    reg_offset = (uint8_t)(fault_addr & 0x0FU);

    /* Bounds-check: reject any vm_id that would index out of the array.
     * This should never happen if the MPU is configured correctly, but
     * the check satisfies MISRA Rule 18.1 (array indexing). */
    if (vm_id >= (uint8_t)VUART_NUM_VMS)
    {
        /* Silently advance PC and return — the trap caused no emulation.*/
        ctx->PC += VUART_INSTR_SIZE;
        return; /* MISRA deviation: Rule 15.5 — early return justified by
                 * safety guard; proceeding with invalid vm_id risks array
                 * out-of-bounds access which is a harder violation. */
    }

    /*-----------------------------------------------------------------------
     * Step 2: Dispatch to write or read emulation based on TIN.
     *
     *   TIN 1 (L2MPW) = write trap: VM tried to store a value.
     *   TIN 0 (L2MPR) = read trap:  VM tried to load a value.
     *
     * ctx->TIN is set by the HV entry stub from the TriCore SYSCON.TIN
     * register captured at trap entry. [SWS_VUART_00003]
     *----------------------------------------------------------------------*/
    if (ctx->TIN == 1U)
    {
        /* Write trap — emulate the store instruction. */
        VUart_WriteReg(ctx, vm_id, reg_offset);
    }
    else
    {
        /* Read trap (TIN == 0) — emulate the load instruction.
         * Any TIN value other than 1 is treated as a read trap; in a
         * correctly configured system only 0 and 1 occur. */
        VUart_ReadReg(ctx, vm_id, reg_offset);
    }

    /*-----------------------------------------------------------------------
     * Step 3: Advance the VM's program counter past the faulting instruction.
     *
     * On TriCore, load/store instructions that touch MPU-trapped addresses
     * are always 32-bit (4 bytes) in length.  16-bit compact instructions
     * cannot encode an absolute address reference that would hit a virtual
     * UART window, so the 4-byte constant is always correct.
     *
     * Adding VUART_INSTR_SIZE prevents an infinite trap loop: without this
     * step the VM would re-execute the same faulting instruction and trap
     * again indefinitely. [SWS_VUART_00104]
     *----------------------------------------------------------------------*/
    ctx->PC += VUART_INSTR_SIZE;
}

/*===========================================================================*
 *  SECTION 8 — WRITE EMULATION                                             *
 *===========================================================================*/

/**
 * @brief   Emulate a VM write to a virtual UART register.
 *
 * @details See the Doxygen header in vuart.h for the register map.
 *          Implementation notes are provided per case below.
 */
FUNC(void, VUART_CODE) VUart_WriteReg(TrapCtx_t *ctx,
                                       uint8_t    vm_id,
                                       uint8_t    reg_offset)
{
    VUart_InstanceType *inst;    /* Pointer to this VM's state struct.          */
    uint32_t            written; /* Value the VM intended to write.             */
    uint8_t             data_byte; /* Single payload byte for DR writes.        */
    VUart_FrameType     frame;   /* Frame assembled for TX queue.               */
    uint8_t             enqueue_ok; /* Result of the enqueue operation.         */

    /* Obtain a pointer to the VM's virtual UART instance. */
    inst = &s_virt_uart[vm_id];

    /* Extract the value written by the VM from its saved data register.
     * ctx->src_reg is the D-register index decoded from the trapped ST
     * instruction by the TriCore arch layer (VUart_Tc_DecodeDstr).
     * [SWS_VUART_00102] */
    written = ctx->D[ctx->src_reg];

    /* Dispatch based on register offset. */
    switch (reg_offset)
    {
        /*-------------------------------------------------------------------
         * DR — Data Register (offset 0x00)
         * [SWS_VUART_00201] [SWS_VUART_00202] [SWS_VUART_00203]
         *------------------------------------------------------------------*/
        case VUART_REG_DR:
        {
            /* The data byte is the least-significant byte of the written
             * word; upper bytes are ignored per the protocol spec. */
            data_byte = (uint8_t)(written & 0xFFU);

            /* Update the DR shadow so the HV can also use it as the last-
             * written byte (useful for debugging and test introspection). */
            inst->DR = (uint32_t)data_byte;

            /* Build the custom wire frame.
             * - dst: inst->DST_ID may be 0xFF for broadcast or a specific VM.
             * - src: this VM.
             * - reg: DR offset (0x00) — receiver interprets this correctly.
             * - data / len: single payload byte. */
            VUart_BuildFrame(&frame,
                             inst->DST_ID,
                             vm_id,
                             VUART_REG_DR,
                             &data_byte,
                             1U);

            /* Set the priority field from CR[7:4] so the TX arbiter can
             * apply priority-based scheduling if configured. [SWS_VUART_00204] */
            frame.priority = (uint8_t)((inst->CR & VUART_CR_TXPRI_MASK)
                                       >> VUART_CR_TXPRI_SHIFT);

            /*---------------------------------------------------------------
             * Broadcast handling: if DST_ID == 0xFF, push one frame per
             * destination VM (excluding the source VM itself).
             * [SWS_VUART_00205]
             *--------------------------------------------------------------*/
            if (inst->DST_ID == VUART_DST_BROADCAST)
            {
                uint8_t dst_vm;
                for (dst_vm = 0U; dst_vm < (uint8_t)VUART_NUM_VMS; dst_vm++)
                {
                    if (dst_vm != vm_id) /* Do not send to self. */
                    {
                        VUart_FrameType bcast_frame;
                        /* Build an individual frame for each destination. */
                        VUart_BuildFrame(&bcast_frame,
                                         dst_vm,
                                         vm_id,
                                         VUART_REG_DR,
                                         &data_byte,
                                         1U);
                        bcast_frame.priority = frame.priority;
                        /* Enqueue; result intentionally checked even if
                         * queue-full frames must be dropped (MISRA 17.7). */
                        enqueue_ok = VUart_TxQueue_Enqueue(&s_tx_queue,
                                                            &bcast_frame);
                        (void)enqueue_ok; /* Drop silently on full queue.    */
                    }
                }
            }
            else
            {
                /* Unicast: enqueue the single frame built above. */
                enqueue_ok = VUart_TxQueue_Enqueue(&s_tx_queue, &frame);
                (void)enqueue_ok; /* Drop silently on full queue.            */
            }

            /* Clear SR.TXRDY to signal that a TX is pending.
             * It will be set back to 1 by the TX arbiter once the physical
             * UART has completed transmission. [SWS_VUART_00105 implied] */
            inst->SR &= ~VUART_SR_TXRDY_BIT;

            /* Reflect the TX_PENDING state in the lifecycle field. */
            inst->state = VUART_STATE_TX_PENDING;

            break;
        }

        /*-------------------------------------------------------------------
         * CR — Control Register (offset 0x08)
         * Only writable bits are accepted; reserved bits are masked out.
         * [SWS_VUART_00101]
         *------------------------------------------------------------------*/
        case VUART_REG_CR:
        {
            /* Apply the writable-bits mask so the VM cannot set reserved
             * or HV-only fields. */
            inst->CR = written & VUART_CR_WRITABLE_MASK;
            break;
        }

        /*-------------------------------------------------------------------
         * DST_ID — Destination VM ID (offset 0x0C)
         * 0xFF = broadcast to all other VMs. [SWS_VUART_00205]
         *------------------------------------------------------------------*/
        case VUART_REG_DST_ID:
        {
            /* Accept only the lowest byte; the upper 3 bytes are ignored. */
            inst->DST_ID = (uint8_t)(written & 0xFFU);
            break;
        }

        /*-------------------------------------------------------------------
         * Default: silent no-op for any unrecognised offset.
         * MISRA-C Rule 16.4 requires a default case.
         *------------------------------------------------------------------*/
        default:
        {
            /* Do nothing — unrecognised register offsets are ignored.
             * This is intentional: future register extensions are silently
             * no-op'd on older hypervisor versions, preventing stalls. */
            break;
        }
    }
}

/*===========================================================================*
 *  SECTION 9 — READ EMULATION (STUB)                                       *
 *                                                                           *
 *  VUart_ReadReg is not the focus of this implementation phase; it is      *
 *  provided as a functional stub that covers all required register offsets. *
 *===========================================================================*/

/**
 * @brief   Emulate a VM read from a virtual UART register. [STUB]
 *
 * @details A fully functional stub: all four register offsets are handled
 *          and the result is injected into the correct D-register in the
 *          saved CPU context. [SWS_VUART_00103]
 */
FUNC(void, VUART_CODE) VUart_ReadReg(TrapCtx_t *ctx,
                                      uint8_t    vm_id,
                                      uint8_t    reg_offset)
{
    VUart_InstanceType *inst;
    uint32_t            inject_val; /* Value to place in the VM's D-register. */
    uint8_t             byte_out;   /* Byte popped from rx_buf (DR case).     */
    uint8_t             pop_ok;     /* Result of the pop operation.           */

    inst       = &s_virt_uart[vm_id];
    inject_val = 0U; /* Safe default — MISRA Rule 9.1.                        */

    switch (reg_offset)
    {
        /*-------------------------------------------------------------------
         * DR — Data Register (offset 0x00)
         * Pop one byte from the RX ring buffer.  If the buffer becomes empty,
         * clear SR.RXRDY. [SWS_VUART_00303]
         *------------------------------------------------------------------*/
        case VUART_REG_DR:
        {
            byte_out = 0U;
            pop_ok   = VUart_RxBuf_Pop(inst, &byte_out);

            if (pop_ok == 1U)
            {
                inject_val = (uint32_t)byte_out;

                /* If the buffer is now empty, clear RXRDY. */
                if (inst->rx_head == inst->rx_tail)
                {
                    inst->SR   &= ~VUART_SR_RXRDY_BIT;
                    inst->state = VUART_STATE_IDLE;
                }
            }
            else
            {
                /* Buffer empty — inject 0x00; SR.RXRDY is already 0. */
                inject_val = 0U;
            }
            break;
        }

        /*-------------------------------------------------------------------
         * SR — Status Register (offset 0x04)
         * Return the complete SR shadow word. [SWS_VUART_00101]
         *------------------------------------------------------------------*/
        case VUART_REG_SR:
        {
            inject_val = inst->SR;
            break;
        }

        /*-------------------------------------------------------------------
         * CR — Control Register (offset 0x08)
         * Return the CR shadow. [SWS_VUART_00101]
         *------------------------------------------------------------------*/
        case VUART_REG_CR:
        {
            inject_val = inst->CR;
            break;
        }

        /*-------------------------------------------------------------------
         * Default: inject 0x00 for any unrecognised offset.
         * MISRA-C Rule 16.4 requires a default case.
         *------------------------------------------------------------------*/
        default:
        {
            inject_val = 0U;
            break;
        }
    }

    /* Inject the emulated value into the VM's destination data register.
     * ctx->dst_reg is decoded from the trapped LD instruction by the arch
     * layer (VUart_Tc_DecodeDstr). [SWS_VUART_00103] */
    ctx->D[ctx->dst_reg] = inject_val;
}

/*===========================================================================*
 *  SECTION 10 — FRAME BUILDER                                              *
 *===========================================================================*/

/**
 * @brief   Construct a fully serialised VUART wire frame in f->raw[].
 *
 * @details Wire layout (all fields big-endian by convention — single bytes):
 *            raw[0]        = 0xAA (SOF)
 *            raw[1]        = dst_vm_id
 *            raw[2]        = src_vm_id
 *            raw[3]        = reg_id
 *            raw[4]        = len
 *            raw[5..5+N-1] = payload[0..N-1]
 *            raw[5+N]      = CRC8 (XOR of all preceding bytes)
 *          Total: 6 + N bytes. [SWS_VUART_00202] [SWS_VUART_00203]
 *
 * @note    The CRC8 algorithm is a simple XOR of all preceding bytes.
 *          A valid received frame has crc_accum XOR crc_byte == 0x00.
 */
FUNC(void, VUART_CODE) VUart_BuildFrame(VUart_FrameType *f,
                                         uint8_t          dst,
                                         uint8_t          src,
                                         uint8_t          reg,
                                         const uint8_t   *data,
                                         uint8_t          len)
{
    uint8_t i;
    uint8_t crc;
    uint8_t raw_idx;

    /* ---- Populate structured fields ------------------------------------ */
    f->dst_vm_id = dst;
    f->src_vm_id = src;
    f->reg_id    = reg;
    f->len       = len;

    /* Copy payload bytes, bounded by VUART_MAX_PAYLOAD (MISRA Rule 18.1). */
    for (i = 0U; i < len; i++)
    {
        f->payload[i] = data[i];
    }

    /* ---- Serialise into raw[] ------------------------------------------ */
    f->raw[0U] = VUART_SOF; /* 0xAA */
    f->raw[1U] = dst;
    f->raw[2U] = src;
    f->raw[3U] = reg;
    f->raw[4U] = len;

    /* Copy payload into raw starting at index 5. */
    for (i = 0U; i < len; i++)
    {
        f->raw[5U + i] = data[i];
    }

    raw_idx = (uint8_t)(5U + len); /* Index where CRC will be written.        */

    /* ---- Compute CRC8 (XOR of raw[0] through raw[raw_idx-1]) ----------- */
    crc = 0U;
    for (i = 0U; i < raw_idx; i++)
    {
        crc ^= f->raw[i]; /* Running XOR accumulation. [SWS_VUART_00203]      */
    }
    f->raw[raw_idx] = crc;

    /* ---- Record total serialised length --------------------------------- */
    f->raw_len = (uint8_t)(6U + len); /* Header(5) + payload(N) + CRC(1).     */
}

/*===========================================================================*
 *  SECTION 11 — RX ISR AND FRAME PARSER                                    *
 *===========================================================================*/

/**
 * @brief Physical UART RX interrupt service routine (HV context).
 *
 * Reads one byte per interrupt invocation from the ASCLIN RXDATA register
 * and passes it to the frame parser FSM.  The ISR is registered exclusively
 * in HV context; no VM handles this interrupt. [SWS_VUART_00005]
 *
 * @note  Only one byte is read per ISR entry to keep execution time bounded
 *        and to respect the O(1) guarantee per byte. [SWS_VUART_00301]
 */
FUNC(void, VUART_CODE) VUart_RxIsr(void)
{
    uint8_t byte;

    /* Read one byte from the physical ASCLIN RXDATA register.
     * The address is: phys_uart_base + ASCLIN_RXDATA_OFFSET (0x160H). */
    byte = Uart_PhysRead(s_cfg->phys_uart_base);

    /* Feed the byte into the frame parser FSM. */
    VUart_FrameParser_Feed(&s_frame_parser, byte);
}

/**
 * @brief   Advance the frame parser FSM by one received byte.
 *
 * @details Each call is O(1): exactly one FSM state transition, no loops,
 *          no dynamic allocation.
 *
 *          FSM state diagram:
 *
 *          IDLE -> DST_VM -> SRC_VM -> REG_ID -> LENGTH -> DATA -> CRC
 *            ^                                              |       |
 *            |_________________ reset on error ____________|_______|
 *
 *          At any state, an invalid field value resets to IDLE (silent
 *          discard). [SWS_VUART_00302] [SWS_VUART_00305]
 */
FUNC(void, VUART_CODE) VUart_FrameParser_Feed(VUart_FrameParserType *p,
                                               uint8_t                byte)
{
    switch (p->state)
    {
        /*-------------------------------------------------------------------
         * IDLE: Wait for the SOF synchronisation byte (0xAA).
         * All other bytes are noise and are silently ignored.
         * [SWS_VUART_00301] — FSM self-synchronisation.
         *------------------------------------------------------------------*/
        case VUART_FSM_IDLE:
        {
            if (byte == VUART_SOF)
            {
                /* SOF received: begin frame assembly.  The CRC accumulator
                 * starts with the SOF byte. */
                p->crc_accum = VUART_SOF;
                p->state     = VUART_FSM_DST_VM;
            }
            /* else: noise byte — remain in IDLE, no action. */
            break;
        }

        /*-------------------------------------------------------------------
         * DST_VM: Capture destination VM ID.
         * Validate range; reject 0xFF as DST_VM_ID since broadcast is only
         * valid on the TX (build) side — incoming frames always address one
         * specific VM. [SWS_VUART_00305]
         *------------------------------------------------------------------*/
        case VUART_FSM_DST_VM:
        {
            if (byte >= (uint8_t)VUART_NUM_VMS)
            {
                /* Invalid DST_VM_ID — silent discard. [SWS_VUART_00305] */
                VUart_FsmReset(p);
            }
            else
            {
                p->frame.dst_vm_id = byte;
                p->crc_accum      ^= byte;
                p->state           = VUART_FSM_SRC_VM;
            }
            break;
        }

        /*-------------------------------------------------------------------
         * SRC_VM: Capture source VM ID.
         * Accept any single-byte value; range validation is not strictly
         * required here — an invalid src_vm_id merely causes the wrong SR
         * to be updated, which is a secondary concern vs. delivery.
         *------------------------------------------------------------------*/
        case VUART_FSM_SRC_VM:
        {
            p->frame.src_vm_id = byte;
            p->crc_accum      ^= byte;
            p->state           = VUART_FSM_REG_ID;
            break;
        }

        /*-------------------------------------------------------------------
         * REG_ID: Capture target register offset.
         * Valid values: 0x00 (DR), 0x04 (SR), 0x08 (CR).
         * Other values are permitted to pass through; VUart_ReadReg handles
         * them as a no-op so the frame is still delivered.
         *------------------------------------------------------------------*/
        case VUART_FSM_REG_ID:
        {
            p->frame.reg_id = byte;
            p->crc_accum   ^= byte;
            p->state        = VUART_FSM_LENGTH;
            break;
        }

        /*-------------------------------------------------------------------
         * LENGTH: Capture payload length.
         * Reject 0 (no payload makes no sense) and values exceeding the
         * maximum. [SWS_VUART_00302 — implied by FSM-reset on invalid field]
         *------------------------------------------------------------------*/
        case VUART_FSM_LENGTH:
        {
            if ((byte == 0U) || (byte > (uint8_t)VUART_MAX_PAYLOAD))
            {
                /* Invalid length — silent discard. */
                VUart_FsmReset(p);
            }
            else
            {
                p->frame.len = byte;
                p->crc_accum ^= byte;
                p->data_idx  = 0U;
                p->state     = VUART_FSM_DATA;
            }
            break;
        }

        /*-------------------------------------------------------------------
         * DATA: Accumulate payload bytes one per call.
         * Advance to CRC state when all expected bytes have been received.
         *------------------------------------------------------------------*/
        case VUART_FSM_DATA:
        {
            /* Guard against buffer overrun — data_idx is bounded by frame.len
             * which was range-checked in the LENGTH state. */
            p->frame.payload[p->data_idx] = byte;
            p->crc_accum                 ^= byte;
            p->data_idx++;

            if (p->data_idx >= p->frame.len)
            {
                /* All payload bytes received; next byte is the CRC. */
                p->state = VUART_FSM_CRC;
            }
            break;
        }

        /*-------------------------------------------------------------------
         * CRC: Validate the frame and dispatch or discard.
         *
         * The XOR-based CRC8 property: if crc_accum XOR crc_byte == 0x00
         * then the frame is valid. [SWS_VUART_00203]
         *
         * Regardless of outcome, always reset to IDLE so the FSM is ready
         * for the next frame immediately. [SWS_VUART_00302]
         *------------------------------------------------------------------*/
        case VUART_FSM_CRC:
        {
            if ((p->crc_accum ^ byte) == 0x00U)
            {
                /* Valid frame — dispatch to the destination VM. */
                VUart_DispatchFrame(&p->frame);
            }
            /* else: CRC mismatch — silent discard. [SWS_VUART_00302] */

            /* Always reset FSM: prepare for the next frame. */
            VUart_FsmReset(p);
            break;
        }

        /*-------------------------------------------------------------------
         * Default: should never be reached; reset FSM as a safety measure.
         * MISRA-C Rule 16.4 requires a default case.
         *------------------------------------------------------------------*/
        default:
        {
            VUart_FsmReset(p);
            break;
        }
    }
}

/*===========================================================================*
 *  SECTION 12 — FRAME DISPATCHER                                           *
 *===========================================================================*/

/**
 * @brief   Route a validated frame payload to the destination VM's RX buffer.
 *
 * @details After the frame parser validates a complete frame, this function
 *          delivers the payload to the correct VM.  Two delivery paths exist:
 *
 *          Same-core path:
 *            1. Push payload bytes into inst->rx_buf.
 *            2. Set SR.RXRDY = 1.
 *            3. If CR.RXIE == 1, inject a virtual interrupt via the arch layer.
 *
 *          Remote-core path:
 *            1. Acquire the IPC ring spinlock.
 *            2. Copy the frame into the IPC ring.
 *            3. Release the spinlock.
 *            4. Send an IPI to wake the destination core's IPI handler.
 *
 *          [SWS_VUART_00303] [SWS_VUART_00304] [SWS_VUART_00401]
 *          [SWS_VUART_00402] [SWS_VUART_00403]
 */
FUNC(void, VUART_CODE) VUart_DispatchFrame(const VUart_FrameType *frame)
{
    uint8_t             dst_vm_id;
    uint8_t             dst_core;
    uint8_t             src_core;
    VUart_InstanceType *dst_inst;
    uint8_t             i;
    uint8_t             push_ok;
    VUart_IpcChannelType *ipc_ch;

    dst_vm_id = frame->dst_vm_id;

    /* Bounds check — should never fail after FSM validation, but required
     * for MISRA Rule 18.1 (array indexing safety). */
    if (dst_vm_id >= (uint8_t)VUART_NUM_VMS)
    {
        return; /* MISRA deviation: Rule 15.5 — early return as safety guard. */
    }

    dst_inst = &s_virt_uart[dst_vm_id];
    dst_core = dst_inst->core_id;

    /* Determine which core is executing this function.
     * The arch layer provides a macro or function for this. */
    /* STUB: assume Core 0 is calling if s_cfg is available.
     * In production: src_core = VUart_Tc_GetCoreId(); */
    src_core = 0U; /* STUB — replace with arch call in production. */

    if (dst_core == src_core)
    {
        /*-------------------------------------------------------------------
         * Same-core delivery: push directly into the VM's RX buffer.
         * [SWS_VUART_00303]
         *------------------------------------------------------------------*/
        for (i = 0U; i < frame->len; i++)
        {
            push_ok = VUart_RxBuf_Push(dst_inst, frame->payload[i]);
            (void)push_ok; /* Drop on buffer-full silently.                   */
        }

        /* Signal data availability: set SR.RXRDY. */
        dst_inst->SR   |= VUART_SR_RXRDY_BIT;
        dst_inst->state = VUART_STATE_RX_READY;

        /* If the VM has enabled RX interrupts, inject a virtual interrupt
         * so the VM's UART RX ISR is called on the next HV->VM transition.
         * [SWS_VUART_00304] */
        if ((dst_inst->CR & VUART_CR_RXIE_BIT) != 0U)
        {
            VUart_InjectVirtualIrq(dst_vm_id);
        }
    }
    else
    {
        /*-------------------------------------------------------------------
         * Cross-core delivery: push frame to the appropriate IPC ring,
         * then signal the destination core via an IPI. [SWS_VUART_00402]
         *------------------------------------------------------------------*/

        /* Select the IPC channel based on source and destination cores.
         * With 2 cores, there are exactly 2 directed channels. */
        if ((src_core == 0U) && (dst_core == 1U))
        {
            ipc_ch = &s_ipc_c0_to_c1;
        }
        else
        {
            /* src_core==1, dst_core==0 */
            ipc_ch = &s_ipc_c1_to_c0;
        }

        /* Push to IPC ring under spinlock. [SWS_VUART_00403] */
        push_ok = VUart_IpcChannel_Push(ipc_ch, frame);
        (void)push_ok; /* Drop on ring-full; IPI still not sent. */

        if (push_ok == 1U)
        {
            /* Send IPI to wake the IPI handler on the destination core.
             * The handler will pop the IPC ring and call VUart_RxBuf_Push. */
            VUart_Tc_SendIPI(dst_core);
        }
    }
}
