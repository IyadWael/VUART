/*
 * Hv_VUart_TestScenarios.h
 *
 * Compile-time Virtual UART test scenario selection.
 *
 * Each scenario maps to a distinct functional case of the VirtualUART feature
 * and exercises configuration-driven behavior.  When the configuration is
 * regenerated or any configuration parameter is changed, recompile under every
 * scenario ID and run the firmware; all assert counters reported via the
 * volatile globals must show zero failures.
 *
 * Traceability:
 * - [SWS_VUART_00001] One VirtualUART instance per configured VM
 * - [SWS_VUART_00002] Virtual UART window allocation
 * - [SWS_VUART_00003] Trap-region based access virtualization
 * - [SWS_VUART_00004] HV-owned physical UART access abstraction
 * - [SWS_VUART_00005] Physical UART RX interrupt routed to hypervisor context
 * - [SWS_VUART_00006] Isolation of per-VM VirtualUART state
 * - [SWS_VUART_00101] Emulated DR, SR, CR, and DST_ID register model
 * - [SWS_VUART_00102] Write trap emulation support
 * - [SWS_VUART_00103] Read trap emulation support
 * - [SWS_VUART_00201] Build/send request initiation from DR write
 * - [SWS_VUART_00302] Frame parsing and validation
 * - [SWS_VUART_00303] Delivery to per-VM RX ring buffer
 * - [SWS_VUART_00304] Invalid destination discard
 * - [SWS_VUART_00401] Uniform VM-visible behavior across cores
 * - [SWS_VUART_00402] Cross-core forwarding through shared memory and IPI
 * - [SWS_VUART_00501] Static software state allocation
 * - [SWS_VUART_00502] Compile-time configured VM/core mapping and buffer sizing
 * - [SWS_VUART_00610] Physical UART owner-core binding through configuration
 */

#ifndef HV_VUART_TEST_SCENARIOS_H_
#define HV_VUART_TEST_SCENARIOS_H_

/*---------------------------------------------------------------------------
 * Scenario identifiers
 *
 * Each identifier selects one coherent set of assertions exercised at runtime.
 * New scenarios are appended at the end so that existing identifiers never
 * change their numeric value.
 *--------------------------------------------------------------------------*/

/** Default build – runs all sanity checks valid for any configuration. */
#define HV_VUART_SCENARIO_DEFAULT                         0u

/* --- Initialization scenarios ------------------------------------------- */

/** Verify that VUart_Cfg_Init populates all configuration objects correctly
 *  according to the current VUART_CFG_* defines. */
#define HV_VUART_SCENARIO_CFG_INIT_CHECK                  1u

/** Verify that VUart_Init clears every instance field and the TX queue on
 *  Core 0 (owner core). */
#define HV_VUART_SCENARIO_INIT_CORE0_OWNER               2u

/** Verify that VUart_Init on a non-owner core does NOT initialize the
 *  physical UART driver and uses a NULL tx_queue. */
#define HV_VUART_SCENARIO_INIT_CORE1_NON_OWNER           3u

/** Verify that all IPC ring heads and tails start at zero after init. */
#define HV_VUART_SCENARIO_INIT_IPC_RINGS_CLEARED         4u

/* --- Register emulation scenarios --------------------------------------- */

/** Write to virtual DR triggers a frame build; SR.TXRDY clears then
 *  reasserts once the frame is dispatched. */
#define HV_VUART_SCENARIO_DR_WRITE_BASIC                  5u

/** Read from virtual DR when the RX ring is empty returns 0 and leaves
 *  SR.RXRDY clear. */
#define HV_VUART_SCENARIO_DR_READ_EMPTY                   6u

/** Read from virtual DR when data is present returns the oldest byte and
 *  removes it from the ring. */
#define HV_VUART_SCENARIO_DR_READ_WITH_DATA               7u

/** Reading the last byte from DR clears SR.RXRDY and resets instance
 *  state to IDLE. */
#define HV_VUART_SCENARIO_DR_READ_LAST_BYTE_CLEARS_RXRDY 8u

/** Write to virtual CR stores only the permitted bit mask:
 *  (CR_EN | CR_RXIE | CR_TXIE | CR_PRIO_MSK). */
#define HV_VUART_SCENARIO_CR_WRITE_MASK                   9u

/** Read from virtual CR returns the last value written through the mask. */
#define HV_VUART_SCENARIO_CR_READ                         10u

/** Write to virtual DST_ID stores the lower 8 bits of the written value. */
#define HV_VUART_SCENARIO_DST_ID_WRITE                    11u

/** Read from virtual DST_ID returns the value previously stored. */
#define HV_VUART_SCENARIO_DST_ID_READ                     12u

/** Read from virtual SR returns the current SR mirror value. */
#define HV_VUART_SCENARIO_SR_READ                         13u

/** Write to virtual SR is silently ignored (SR is read-only from VM). */
#define HV_VUART_SCENARIO_SR_WRITE_IGNORED                14u

/* --- Frame construction scenarios --------------------------------------- */

/** VUart_BuildFrame stores src/dst/len and first payload byte correctly. */
#define HV_VUART_SCENARIO_BUILD_FRAME_BASIC               15u

/** VUart_BuildFrame clamps oversized len to VUART_MAX_PAYLOAD. */
#define HV_VUART_SCENARIO_BUILD_FRAME_CLAMP_LEN           16u

/** VUart_BuildFrame with NULL frame pointer does not crash. */
#define HV_VUART_SCENARIO_BUILD_FRAME_NULL_PTR            17u

/* --- Parser FSM scenarios ----------------------------------------------- */

/** Feed a well-formed single-byte-payload frame byte by byte through
 *  VUart_FrameParser_Feed; verify DispatchFrame is triggered. */
#define HV_VUART_SCENARIO_PARSER_VALID_FRAME              18u

/** A dst_vm_id >= VUART_NUM_VMS in the first byte resets the parser. */
#define HV_VUART_SCENARIO_PARSER_INVALID_DST              19u

/** A zero length byte resets the parser without delivering any frame. */
#define HV_VUART_SCENARIO_PARSER_ZERO_LENGTH              20u

/** A length byte larger than VUART_MAX_PAYLOAD resets the parser. */
#define HV_VUART_SCENARIO_PARSER_OVERSIZED_LENGTH         21u

/** Two back-to-back valid frames parsed in sequence both get dispatched. */
#define HV_VUART_SCENARIO_PARSER_CONSECUTIVE_FRAMES       22u

/** A partial frame followed by a reset byte restarts cleanly. */
#define HV_VUART_SCENARIO_PARSER_PARTIAL_THEN_RESET       23u

/* --- RX ring buffer scenarios ------------------------------------------- */

/** Push a single byte; verify rx_head advances and byte is readable. */
#define HV_VUART_SCENARIO_RX_RING_PUSH_SINGLE             24u

/** Fill the ring to capacity; the next push returns 0 and sets SR.OVERRUN. */
#define HV_VUART_SCENARIO_RX_RING_OVERFLOW                25u

/** Pop from an empty ring does not corrupt state. */
#define HV_VUART_SCENARIO_RX_RING_POP_EMPTY               26u

/** Push RX_BUF_SIZE-1 bytes (full ring), pop all of them in order. */
#define HV_VUART_SCENARIO_RX_RING_FILL_THEN_DRAIN         27u

/** Ring index wrap-around: push to end, pop, push again crosses index 0. */
#define HV_VUART_SCENARIO_RX_RING_WRAP_AROUND             28u

/* --- TX queue scenarios ------------------------------------------------- */

/** Push one frame into TX queue; pop returns same frame. */
#define HV_VUART_SCENARIO_TX_QUEUE_PUSH_POP               29u

/** Fill TX queue to TX_QUEUE_DEPTH-1; the next push returns 0. */
#define HV_VUART_SCENARIO_TX_QUEUE_FULL                   30u

/** Pop from empty TX queue returns 0 and does not modify caller frame. */
#define HV_VUART_SCENARIO_TX_QUEUE_POP_EMPTY              31u

/** TX queue correctly wraps index around after multiple push/pop cycles. */
#define HV_VUART_SCENARIO_TX_QUEUE_WRAP_AROUND            32u

/* --- IPC ring scenarios ------------------------------------------------- */

/** Push one frame into an IPC ring and pop it back. */
#define HV_VUART_SCENARIO_IPC_RING_PUSH_POP               33u

/** Fill IPC ring; next push returns 0. */
#define HV_VUART_SCENARIO_IPC_RING_FULL                   34u

/** Pop from empty IPC ring returns 0. */
#define HV_VUART_SCENARIO_IPC_RING_POP_EMPTY              35u

/** IPC index wraps after VUART_IPC_RING_DEPTH iterations. */
#define HV_VUART_SCENARIO_IPC_RING_WRAP_AROUND            36u

/* --- Dispatch / routing scenarios --------------------------------------- */

/** Same-core dispatch: frame whose dst VM lives on the calling core is
 *  delivered via VUart_DeliverLocal without touching any IPC ring. */
#define HV_VUART_SCENARIO_DISPATCH_LOCAL                  37u

/** Cross-core dispatch: frame whose dst VM lives on another core is pushed
 *  to the IPC ring and VUart_Tc_SendIPI is called. */
#define HV_VUART_SCENARIO_DISPATCH_CROSS_CORE             38u

/** Frame with dst_vm_id > VUART_NUM_VMS is discarded by DispatchFrame. */
#define HV_VUART_SCENARIO_DISPATCH_INVALID_DST            39u

/** Deliver frame when the destination VM instance does not exist locally. */
#define HV_VUART_SCENARIO_DELIVER_LOCAL_NO_INSTANCE       40u

/* --- Virtual IRQ injection scenarios ------------------------------------ */

/** DeliverLocal with CR.RXIE set calls VUart_InjectVirtualIrq. */
#define HV_VUART_SCENARIO_VIRTUAL_IRQ_INJECTED_ON_RXIE    41u

/** DeliverLocal with CR.RXIE clear does NOT call VUart_InjectVirtualIrq. */
#define HV_VUART_SCENARIO_VIRTUAL_IRQ_NOT_INJECTED        42u

/** VUart_InjectVirtualIrq sets the correct bit in the bitmap. */
#define HV_VUART_SCENARIO_VIRTUAL_IRQ_BITMAP_BIT          43u

/* --- VM isolation scenarios --------------------------------------------- */

/** Write to VM0 instance does not alter VM1 or VM2 instance fields. */
#define HV_VUART_SCENARIO_VM_ISOLATION_WRITE              44u

/** RX delivery to VM0 does not alter VM1 or VM2 RX ring state. */
#define HV_VUART_SCENARIO_VM_ISOLATION_RX                 45u

/* --- Configuration consistency scenarios -------------------------------- */

/** VUART_NUM_VMS matches VUART_CFG_NUM_VMS at compile time and the
 *  kVmBaseAddr array length equals VUART_NUM_VMS. */
#define HV_VUART_SCENARIO_CFG_NUM_VMS_CONSISTENT          46u

/** VUART_NUM_CORES matches VUART_CFG_NUM_CORES and the config array
 *  length is VUART_NUM_CORES. */
#define HV_VUART_SCENARIO_CFG_NUM_CORES_CONSISTENT        47u

/** RX_BUF_SIZE equals VUART_CFG_RX_BUF_SIZE and is a power of two. */
#define HV_VUART_SCENARIO_CFG_RX_BUF_SIZE_POW2            48u

/** TX_QUEUE_DEPTH equals VUART_CFG_TX_QUEUE_DEPTH and is a power of two. */
#define HV_VUART_SCENARIO_CFG_TX_DEPTH_POW2               49u

/** VUART_MAX_PAYLOAD equals VUART_CFG_MAX_PAYLOAD. */
#define HV_VUART_SCENARIO_CFG_MAX_PAYLOAD_CONSISTENT      50u

/** VUART_IPC_RING_DEPTH equals VUART_CFG_IPC_RING_DEPTH. */
#define HV_VUART_SCENARIO_CFG_IPC_RING_DEPTH_CONSISTENT   51u

/** VUART_VM_WINDOW_SIZE equals VUART_CFG_VM_WINDOW_SIZE. */
#define HV_VUART_SCENARIO_CFG_WINDOW_SIZE_CONSISTENT      52u

/** Each VM base address in kVmBaseAddr matches the corresponding
 *  VUART_CFG_VMn_WINDOW_BASE define. */
#define HV_VUART_SCENARIO_CFG_VM_BASE_ADDRS               53u

/** kVmCoreMap contains valid core IDs (< VUART_NUM_CORES) for all VMs. */
#define HV_VUART_SCENARIO_CFG_VM_CORE_MAP_VALID           54u

/** Owner core field in every VUart_kConfigs entry equals
 *  VUART_CFG_UART0_OWNER_CORE. */
#define HV_VUART_SCENARIO_CFG_OWNER_CORE_BINDING          55u

/** phys_uart_base in every VUart_kConfigs entry equals
 *  VUART_CFG_PHYS_UART0_BASE. */
#define HV_VUART_SCENARIO_CFG_PHYS_BASE_BINDING           56u

/** rx_isr_node in every VUart_kConfigs entry equals
 *  VUART_CFG_UART0_RX_ISR_NODE. */
#define HV_VUART_SCENARIO_CFG_RX_ISR_NODE_BINDING         57u

/** Core 0 config has tx_queue != NULL_PTR; non-owner cores that are
 *  configured do NOT hold a NULL tx_queue pointer (they receive their
 *  own local queue). */
#define HV_VUART_SCENARIO_CFG_TX_QUEUE_PTR                58u

/** Total instance count across all cores equals VUART_CFG_NUM_VMS. */
#define HV_VUART_SCENARIO_CFG_INSTANCE_COUNT_TOTAL        59u

/* --- Error / boundary scenarios ----------------------------------------- */

/** VUart_WriteReg with an invalid vm_id > VUART_NUM_VMS does nothing. */
#define HV_VUART_SCENARIO_WRITE_INVALID_VM_ID             60u

/** VUart_ReadReg with an invalid vm_id > VUART_NUM_VMS does nothing. */
#define HV_VUART_SCENARIO_READ_INVALID_VM_ID              61u

/** DR write with size 0 is ignored (no frame is built). */
#define HV_VUART_SCENARIO_DR_WRITE_ZERO_SIZE              62u

/** DR write with size > VUART_MAX_PAYLOAD is clamped in BuildFrame but
 *  VUart_WriteReg rejects it before building. */
#define HV_VUART_SCENARIO_DR_WRITE_OVERSIZED              63u

/** VUart_Cfg_GetByCoreId returns NULL_PTR for a core_id >=
 *  VUART_NUM_CORES. */
#define HV_VUART_SCENARIO_CFG_GET_INVALID_CORE            64u

/** VUart_FindLocalInstanceByVmId returns NULL_PTR when no local instance
 *  has that VM id. */
#define HV_VUART_SCENARIO_FIND_INSTANCE_NOT_FOUND         65u

/** VUart_RxRingPush with a NULL inst pointer returns 0. */
#define HV_VUART_SCENARIO_RX_PUSH_NULL_INST               66u

/* -------------------------------------------------------------------------
 * Default scenario selection
 *-------------------------------------------------------------------------*/
#ifndef HV_VUART_CFG_SCENARIO
#define HV_VUART_CFG_SCENARIO HV_VUART_SCENARIO_DEFAULT
#endif

#endif /* HV_VUART_TEST_SCENARIOS_H_ */
