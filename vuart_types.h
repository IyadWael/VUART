/**
 * @file    vuart_types.h
 * @brief   VirtualUART (VUART) — Type Definitions, Constants, and Structures
 *
 * @details This header defines every data type, enumeration, and compile-time
 *          constant required by the VirtualUART subsystem of the ImperioHV
 *          hypervisor.  All definitions conform to MISRA-C:2012.
 *
 *          Target Hardware : Infineon AURIX TC499 (TriCore architecture)
 *          Hypervisor      : ImperioHV — Static Partitioning, Type-1 bare-metal
 *          Sponsor         : Siemens
 *
 * @note    No dynamic memory allocation is used anywhere in this subsystem.
 *          All buffers and queues are statically allocated at compile time,
 *          satisfying [SWS_VUART_00501].
 *
 * @note    MISRA-C:2012 compliance notes:
 *          - All integer types are sourced from <stdint.h> (Rule 4.6).
 *          - Boolean-like fields use uint8_t with defined 0/1 values (Rule 14.4).
 *          - volatile is applied to every shared index modified by both
 *            a producer context and a consumer context (Rules 8.6, 9.1).
 *
 * @version 1.0
 * @date    2025
 */

#ifndef VUART_TYPES_H
#define VUART_TYPES_H

/*===========================================================================*
 *  1.  STANDARD TYPE HEADERS                                                *
 *===========================================================================*/
#include <stdint.h>   /* uint8_t, uint16_t, uint32_t — MISRA Rule 4.6 */
#include <stddef.h>   /* size_t                                         */

/*===========================================================================*
 *  2.  COMPILE-TIME CONFIGURATION CONSTANTS                                 *
 *      Adjust these to match the actual system partition layout.            *
 *      [SWS_VUART_00502]                                                    *
 *===========================================================================*/

/** Maximum number of VMs supported across all cores. */
#define VUART_NUM_VMS           ((uint8_t)4U)

/** Number of physical cores in the system. */
#define VUART_NUM_CORES         ((uint8_t)2U)

/** Maximum VMs per single core (ceiling of NUM_VMS / NUM_CORES). */
#define VUART_MAX_VMS_PER_CORE  ((uint8_t)2U)

/**
 * @brief Size (bytes) of each per-VM receive ring buffer.
 *
 * Must be a power of 2 so the modulo-masking optimisation is valid.
 * [SWS_VUART_00503]
 */
#define VUART_RX_BUF_SIZE       ((uint8_t)64U)

/**
 * @brief Maximum payload bytes in a single VUART frame.
 *
 * The wire format LENGTH field is 1 byte; valid range is 1–64.
 * [SWS_VUART_00202]
 */
#define VUART_MAX_PAYLOAD       ((uint8_t)64U)

/**
 * @brief Total bytes in a serialised frame (SOF + 4-byte header + payload + CRC8).
 *
 * raw_len = 6 + len  (minimum 7 when len=1, maximum 70 when len=64).
 */
#define VUART_FRAME_MAX_BYTES   ((uint8_t)(6U + VUART_MAX_PAYLOAD))

/** Depth of the shared TX arbitration queue (number of frame slots). */
#define VUART_TX_QUEUE_DEPTH    ((uint8_t)16U)

/** Depth of each IPC ring between cores (number of frame slots). */
#define VUART_IPC_RING_DEPTH    ((uint8_t)8U)

/*---------------------------------------------------------------------------*
 *  Frame-protocol constants  [SWS_VUART_00202]                              *
 *---------------------------------------------------------------------------*/

/** Start-of-frame synchronisation byte. */
#define VUART_SOF               ((uint8_t)0xAAU)

/** Broadcast destination VM identifier. [SWS_VUART_00205] */
#define VUART_DST_BROADCAST     ((uint8_t)0xFFU)

/*---------------------------------------------------------------------------*
 *  Virtual register offsets within a VM's MPU window                        *
 *  [SWS_VUART_00101]                                                        *
 *---------------------------------------------------------------------------*/

/** Data Register — offset within the virtual UART window. */
#define VUART_REG_DR            ((uint8_t)0x00U)

/** Status Register — offset within the virtual UART window. */
#define VUART_REG_SR            ((uint8_t)0x04U)

/** Control Register — offset within the virtual UART window. */
#define VUART_REG_CR            ((uint8_t)0x08U)

/** Destination-ID Register — offset within the virtual UART window. */
#define VUART_REG_DST_ID        ((uint8_t)0x0CU)

/*---------------------------------------------------------------------------*
 *  SR (Status Register) shadow bit positions                                *
 *---------------------------------------------------------------------------*/

/** SR bit 0: TX Ready — mirrors ASCLIN FLAGS.TC (bit 17). */
#define VUART_SR_TXRDY_BIT      ((uint32_t)0x00000001UL)

/** SR bit 1: RX Ready — set when rx_buf is non-empty. */
#define VUART_SR_RXRDY_BIT      ((uint32_t)0x00000002UL)

/** SR bit 2: Peripheral Busy. */
#define VUART_SR_BUSY_BIT       ((uint32_t)0x00000004UL)

/*---------------------------------------------------------------------------*
 *  CR (Control Register) shadow bit positions                               *
 *---------------------------------------------------------------------------*/

/** CR bit 0: UART Enable. */
#define VUART_CR_EN_BIT         ((uint32_t)0x00000001UL)

/** CR bit 1: RX Interrupt Enable. [SWS_VUART_00304] */
#define VUART_CR_RXIE_BIT       ((uint32_t)0x00000002UL)

/** CR bit 2: TX Interrupt Enable. */
#define VUART_CR_TXIE_BIT       ((uint32_t)0x00000004UL)

/** CR bits [7:4]: TX priority (0 = lowest, 15 = highest). [SWS_VUART_00204] */
#define VUART_CR_TXPRI_MASK     ((uint32_t)0x000000F0UL)
#define VUART_CR_TXPRI_SHIFT    ((uint32_t)4U)

/**
 * @brief Mask of all writable CR bits.
 *
 * Only bits explicitly defined in the spec are accepted from the VM;
 * all other bits are silently masked out. [SWS_VUART_00101]
 */
#define VUART_CR_WRITABLE_MASK  ((uint32_t)(VUART_CR_EN_BIT   | \
                                             VUART_CR_RXIE_BIT | \
                                             VUART_CR_TXIE_BIT | \
                                             VUART_CR_TXPRI_MASK))

/*---------------------------------------------------------------------------*
 *  Physical ASCLIN register offsets  (relative to phys_uart_base)           *
 *---------------------------------------------------------------------------*/

#define ASCLIN_TXDATA_OFFSET    ((uint32_t)0x140U)
#define ASCLIN_RXDATA_OFFSET    ((uint32_t)0x160U)
#define ASCLIN_FLAGS_OFFSET     ((uint32_t)0x12CU)
#define ASCLIN_RXFIFOCON_OFFSET ((uint32_t)0x108U)
#define ASCLIN_TXFIFOCON_OFFSET ((uint32_t)0x104U)

/*---------------------------------------------------------------------------*
 *  TriCore PC advancement                                                   *
 *  On AURIX TriCore, standard 32-bit instructions are 4 bytes wide.         *
 *  16-bit compact instructions (suffix "16") are 2 bytes wide.              *
 *  The trap handler always sees a 32-bit ST/LD instruction from the VM,     *
 *  so VUART_INSTR_SIZE is 4.  [SWS_VUART_00104]                             *
 *---------------------------------------------------------------------------*/
#define VUART_INSTR_SIZE        ((uint32_t)4U)

/*===========================================================================*
 *  3.  ENUMERATIONS                                                         *
 *===========================================================================*/

/**
 * @brief Operational lifecycle state of one virtual UART instance.
 */
typedef enum
{
    VUART_STATE_IDLE       = 0U, /**< Initialised, no pending operation.      */
    VUART_STATE_TX_PENDING = 1U, /**< Frame queued; awaiting physical TX.     */
    VUART_STATE_RX_READY   = 2U, /**< At least one byte in rx_buf.            */
    VUART_STATE_ERROR      = 3U  /**< Unrecoverable internal error.           */
} VUart_StateType;

/**
 * @brief TX arbitration policy for the shared TX queue. [SWS_VUART_00204]
 */
typedef enum
{
    VUART_ARB_FIFO     = 0U, /**< First-in, first-out dequeue order.          */
    VUART_ARB_PRIORITY = 1U  /**< Higher CR[7:4] value dequeued first.        */
} VUart_ArbPolicyType;

/**
 * @brief States of the byte-by-byte frame parser FSM. [SWS_VUART_00301]
 *
 * Transition order: IDLE -> DST_VM -> SRC_VM -> REG_ID -> LENGTH -> DATA -> CRC
 */
typedef enum
{
    VUART_FSM_IDLE    = 0U, /**< Waiting for SOF (0xAA) byte.                 */
    VUART_FSM_DST_VM  = 1U, /**< Next byte is DST_VM_ID.                      */
    VUART_FSM_SRC_VM  = 2U, /**< Next byte is SRC_VM_ID.                      */
    VUART_FSM_REG_ID  = 3U, /**< Next byte is REG_ID.                         */
    VUART_FSM_LENGTH  = 4U, /**< Next byte is LENGTH.                         */
    VUART_FSM_DATA    = 5U, /**< Accumulating payload bytes.                  */
    VUART_FSM_CRC     = 6U  /**< Next byte is CRC8; validate and dispatch.    */
} VUart_FsmStateType;

/*===========================================================================*
 *  4.  TRAP CONTEXT STRUCTURE                                               *
 *      Represents the saved CPU state captured by the HV entry stub when   *
 *      an L2MPR/L2MPW trap fires. The HV uses this to extract operand      *
 *      values (write path) and to inject results (read path).              *
 *      [SWS_VUART_00102] [SWS_VUART_00103] [SWS_VUART_00104]              *
 *===========================================================================*/

/**
 * @brief Saved CPU context passed to the virtual UART trap handler.
 *
 * @note  On TriCore, the general-purpose register file is split into
 *        - D[0..15]: 32-bit data registers (arithmetic/logic operands).
 *        - A[0..15]: 32-bit address registers (pointer operands).
 *        Both sets are saved in full by the HV entry stub so that register
 *        emulation can read source operands and write destination operands
 *        back into the context before the trap return.
 *
 * @note  The DSTR (Data-Store/Load Target Register) encoding is extracted
 *        from the trapped instruction word by VUart_Tc_DecodeDstr() in the
 *        arch layer; the result is stored in src_reg / dst_reg fields so
 *        that VUart_WriteReg / VUart_ReadReg need not decode opcodes.
 */
typedef struct
{
    uint32_t D[16U];     /**< Saved data registers D[0]..D[15].              */
    uint32_t A[16U];     /**< Saved address registers A[0]..A[15].           */
    uint32_t PC;         /**< Saved program counter (address of faulting
                          *   instruction). Advanced by VUART_INSTR_SIZE
                          *   before trap return.                             */
    uint32_t PSW;        /**< Saved program-status word.                     */
    uint8_t  TIN;        /**< Trap Identification Number: 0=L2MPR, 1=L2MPW. */
    uint8_t  src_reg;    /**< D-register index holding the write value
                          *   (decoded from the trapped ST instruction).     */
    uint8_t  dst_reg;    /**< D-register index that receives the read result
                          *   (decoded from the trapped LD instruction).     */
    uint8_t  _pad;       /**< Explicit padding — MISRA Rule 6.1.             */
    uint32_t fault_addr; /**< Virtual address that caused the trap.          */
} TrapCtx_t;

/*===========================================================================*
 *  5.  FRAME TYPE                                                           *
 *      Represents one complete custom wire frame used by the frame builder, *
 *      TX queue, and frame parser FSM. [SWS_VUART_00202]                   *
 *===========================================================================*/

/**
 * @brief Custom VUART wire frame.
 *
 * Wire layout: SOF | DST_VM_ID | SRC_VM_ID | REG_ID | LENGTH | PAYLOAD | CRC8
 * Total size  :  1  +    1     +     1     +   1    +    1   +  N (1-64) +  1
 *             = 6 + N bytes. [SWS_VUART_00202]
 */
typedef struct
{
    uint8_t dst_vm_id;                   /**< Destination VM — routing key.  */
    uint8_t src_vm_id;                   /**< Source VM — for SR.TXRDY ack.  */
    uint8_t reg_id;                      /**< Target virt. register offset.  */
    uint8_t payload[VUART_MAX_PAYLOAD];  /**< Payload data bytes.            */
    uint8_t len;                         /**< Valid bytes in payload[].       */
    uint8_t raw[VUART_FRAME_MAX_BYTES];  /**< Fully serialised frame.        */
    uint8_t raw_len;                     /**< Total bytes in raw[].           */
    uint8_t priority;                    /**< CR[7:4] of src VM — used by
                                          *   priority arbiter.              */
    uint8_t _pad[1U];                    /**< Explicit padding — MISRA 6.1.  */
} VUart_FrameType;

/*===========================================================================*
 *  6.  SPINLOCK TYPE                                                        *
 *      Minimal spinlock for multi-core critical sections.                   *
 *      The actual test-and-set is provided by the arch layer.               *
 *===========================================================================*/

/**
 * @brief Spinlock — a single volatile word polled in a tight loop.
 *
 * 0 = unlocked, 1 = locked.  The arch layer uses LDMST/SWAPMSK or
 * the TriCore CMPSWAP instruction for atomicity.
 */
typedef volatile uint32_t spinlock_t;

/*===========================================================================*
 *  7.  TX QUEUE TYPE                                                        *
 *      Shared across all VMs and cores.  Single producer per slot; single  *
 *      consumer (TX arbiter). [SWS_VUART_00501]                            *
 *===========================================================================*/

/**
 * @brief Shared TX arbitration queue. [SWS_VUART_00204]
 *
 * @note  head is written under spinlock by any core's trap handler.
 *        tail is read/written only by the single TX arbiter task/ISR.
 *        This MPSC (multi-producer, single-consumer) pattern is safe
 *        with the spinlock protecting head updates.
 */
typedef struct
{
    VUart_FrameType      frames[VUART_TX_QUEUE_DEPTH]; /**< Statically alloc'd slots. */
    volatile uint8_t     head;    /**< Write index (next free slot).           */
    volatile uint8_t     tail;    /**< Read index (next frame to transmit).    */
    spinlock_t           lock;    /**< Protects head from concurrent updates.  */
    VUart_ArbPolicyType  policy;  /**< FIFO or PRIORITY dequeue policy.        */
} VUart_TxQueueType;

/*===========================================================================*
 *  8.  IPC CHANNEL TYPE                                                     *
 *      One ring per ordered core pair.  Producer: RX ISR / DispatchFrame   *
 *      on the source core.  Consumer: IPI handler on the destination core. *
 *      [SWS_VUART_00402] [SWS_VUART_00403]                                *
 *===========================================================================*/

/**
 * @brief Single-direction inter-core frame channel (SPSC ring).
 *
 * Located in shared memory visible to both cores.
 */
typedef struct
{
    VUart_FrameType  frames[VUART_IPC_RING_DEPTH]; /**< Frame ring slots.    */
    volatile uint8_t head;   /**< Write index — producer core.                */
    volatile uint8_t tail;   /**< Read index  — consumer core.                */
    spinlock_t       lock;   /**< Protects head on the producer side.         */
} VUart_IpcChannelType;

/*===========================================================================*
 *  9.  VIRTUAL UART INSTANCE (PER-VM STATE)                                *
 *      One instance per VM, stored in HV-private memory.                   *
 *      [SWS_VUART_00001]                                                   *
 *===========================================================================*/

/**
 * @brief Complete software model of the emulated UART for one VM.
 *
 * All fields are managed exclusively by the hypervisor; no VM has direct
 * read or write access. [SWS_VUART_00006]
 */
typedef struct
{
    uint8_t          vm_id;                   /**< Unique VM index (0..NUM_VMS-1). */
    uint8_t          core_id;                 /**< Physical core this VM runs on.  */
    uint8_t          _pad0[2U];               /**< Explicit padding — MISRA 6.1.   */
    uint32_t         virt_base;               /**< Base addr of MPU-protected window.*/

    /* --- Virtual register shadows ---------------------------------------- */
    uint32_t         DR;    /**< Data Register shadow.                         */
    uint32_t         SR;    /**< Status Register shadow (TXRDY, RXRDY, BUSY). */
    uint32_t         CR;    /**< Control Register shadow (EN, RXIE, TXIE, PRI).*/
    uint8_t          DST_ID;/**< Destination VM ID for next TX; 0xFF=broadcast.*/
    uint8_t          _pad1[3U];               /**< Explicit padding.           */

    /* --- RX ring buffer -------------------------------------------------- */
    uint8_t          rx_buf[VUART_RX_BUF_SIZE]; /**< Statically allocated RX ring. */
    volatile uint8_t rx_head; /**< Write index. Producer: RX ISR / IPI handler.*/
    volatile uint8_t rx_tail; /**< Read index.  Consumer: HV trap handler.     */
    uint8_t          _pad2[2U];               /**< Explicit padding.           */

    /* --- Lifecycle -------------------------------------------------------- */
    VUart_StateType  state; /**< IDLE | TX_PENDING | RX_READY | ERROR.         */
} VUart_InstanceType;

/*===========================================================================*
 *  10. FRAME PARSER FSM STATE                                               *
 *      One static instance per physical UART. O(1) per byte, no heap.     *
 *      [SWS_VUART_00301]                                                   *
 *===========================================================================*/

/**
 * @brief Complete state of the byte-by-byte frame parser FSM.
 */
typedef struct
{
    VUart_FsmStateType state;      /**< Current FSM state.                    */
    VUart_FrameType    frame;      /**< Frame being assembled field-by-field.  */
    uint8_t            data_idx;   /**< Payload bytes received so far.         */
    uint8_t            crc_accum;  /**< Running XOR. Valid frame => 0x00.      */
    uint8_t            _pad[2U];   /**< Explicit padding — MISRA 6.1.         */
} VUart_FrameParserType;

/*===========================================================================*
 *  11. TOP-LEVEL FEATURE CONFIGURATION                                     *
 *      One instance per core, passed to VUart_Init(). [SWS_VUART_00502]   *
 *===========================================================================*/

/**
 * @brief HV-level compile-time configuration for the VirtualUART feature.
 */
typedef struct
{
    VUart_InstanceType  *instances;       /**< Array of VM instances on this core.   */
    uint8_t              num_instances;   /**< Number of VMs on this core.            */
    uint8_t              rx_isr_node;     /**< HW interrupt node for physical UART RX.*/
    uint8_t              _pad[2U];        /**< Explicit padding — MISRA 6.1.          */
    uint32_t             phys_uart_base;  /**< Physical ASCLIN base address.          */
    VUart_TxQueueType   *tx_queue;        /**< Shared TX queue pointer (shared mem).  */
    const uint8_t       *vm_core_map;     /**< vm_id -> core_id mapping array.        */
} VUart_ConfigType;

#endif /* VUART_TYPES_H */
