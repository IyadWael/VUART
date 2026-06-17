/*
 * Hv_VUart_Test.h
 *
 * Optional Virtual UART test instrumentation.
 *
 * Usage
 * -----
 * Define both HV_TEST_ENABLE and HV_VUART_TEST_ENABLE to 1u (e.g. via
 * compiler flags or in a test-specific makefile) to activate the
 * instrumentation. When either macro is 0 every macro expands to a no-op so
 * that no code is added to the production build.
 *
 * Hit points
 * ----------
 * Call HV_VUART_TEST_HIT(HV_VUART_HIT_xxx) at the start and at the
 * significant exit paths of each VirtualUART function.  The hit mask
 * accumulates which code paths have been exercised; a coverage report can be
 * obtained by reading g_hv_vuart_test_hit_mask after the scenario runs.
 *
 * Assertions
 * ----------
 * Use HV_VUART_TEST_ASSERT_TRUE and HV_VUART_TEST_ASSERT_EQ_U32 to verify
 * expected state.  Both macros increment g_hv_vuart_test_assert_total; the
 * result determines whether g_hv_vuart_test_assert_passed or
 * g_hv_vuart_test_assert_failed is incremented.  The id of the last failed
 * assertion is recorded in g_hv_vuart_test_last_failed_id.
 *
 * After every test scenario the host debugger or a background task reads:
 *   g_hv_vuart_test_assert_failed == 0  =>  PASS
 *   g_hv_vuart_test_assert_failed != 0  =>  FAIL, see last_failed_id
 *
 * Traceability:
 * - [SWS_VUART_00001] through [SWS_VUART_00610] (all requirements exercised
 *   through the scenarios defined in Hv_VUart_TestScenarios.h)
 */

#ifndef HV_VUART_TEST_H_
#define HV_VUART_TEST_H_

#include "Std_Types.h"
#include "Hv_VUart_TestScenarios.h"

#ifndef HV_TEST_ENABLE
#define HV_TEST_ENABLE 0u
#endif

#ifndef HV_VUART_TEST_ENABLE
#define HV_VUART_TEST_ENABLE 0u
#endif

#if (HV_TEST_ENABLE != 0u) && (HV_VUART_TEST_ENABLE != 0u)

/* -------------------------------------------------------------------------
 * Observable state variables
 *
 * All variables are volatile so that a connected debugger can read them
 * live without the optimizer collapsing the stores.
 *-------------------------------------------------------------------------*/

/** Currently active test scenario (mirrors HV_VUART_CFG_SCENARIO). */
extern volatile uint32 g_hv_vuart_test_active_scenario;

/* ---- Last observed values captured by hit callbacks -------------------- */

/** VM id of the most recently exercised VirtualUART instance. */
extern volatile uint32 g_hv_vuart_test_last_vm_id;

/** Register offset (byte) of the most recent emulated access. */
extern volatile uint32 g_hv_vuart_test_last_reg_offset;

/** Value written in the most recent emulated DR write. */
extern volatile uint32 g_hv_vuart_test_last_written_val;

/** Value injected in the most recent emulated DR read. */
extern volatile uint32 g_hv_vuart_test_last_injected_val;

/** Source core of the most recently dispatched cross-core frame. */
extern volatile uint32 g_hv_vuart_test_last_src_core;

/** Destination core of the most recently dispatched cross-core frame. */
extern volatile uint32 g_hv_vuart_test_last_dst_core;

/** Return value of the most recent VUart_IpcPushFrame call (0 or 1). */
extern volatile uint32 g_hv_vuart_test_last_ipc_push_result;

/** Return value of the most recent VUart_IpcPopFrame call (0 or 1). */
extern volatile uint32 g_hv_vuart_test_last_ipc_pop_result;

/** Return value of the most recent VUart_TxQueuePush call (0 or 1). */
extern volatile uint32 g_hv_vuart_test_last_tx_push_result;

/** Return value of the most recent VUart_TxQueuePop call (0 or 1). */
extern volatile uint32 g_hv_vuart_test_last_tx_pop_result;

/** Return value of the most recent VUart_RxRingPush call (0 or 1). */
extern volatile uint32 g_hv_vuart_test_last_rx_push_result;

/* ---- Event counters ---------------------------------------------------- */

/** Number of times VUart_BuildFrame was called. */
extern volatile uint32 g_hv_vuart_test_build_frame_count;

/** Number of times VUart_FrameParser_Feed completed a valid frame. */
extern volatile uint32 g_hv_vuart_test_parser_complete_count;

/** Number of times the parser FSM was reset (invalid / partial frames). */
extern volatile uint32 g_hv_vuart_test_parser_reset_count;

/** Number of times VUart_DispatchFrame sent a frame to a local instance. */
extern volatile uint32 g_hv_vuart_test_dispatch_local_count;

/** Number of times VUart_DispatchFrame forwarded a frame via IPC. */
extern volatile uint32 g_hv_vuart_test_dispatch_ipc_count;

/** Number of times VUart_DispatchFrame discarded an invalid frame. */
extern volatile uint32 g_hv_vuart_test_dispatch_discard_count;

/** Number of times VUart_InjectVirtualIrq was called. */
extern volatile uint32 g_hv_vuart_test_virtual_irq_count;

/** Number of times VUart_TxKick was called. */
extern volatile uint32 g_hv_vuart_test_tx_kick_count;

/** Number of times VUart_IpiRxHandler was entered. */
extern volatile uint32 g_hv_vuart_test_ipi_rx_count;

/** Number of times VUart_RxIsr was entered. */
extern volatile uint32 g_hv_vuart_test_rx_isr_count;

/* ---- Assert accounting ------------------------------------------------- */

/** Total number of assertions evaluated. */
extern volatile uint32 g_hv_vuart_test_assert_total;

/** Number of assertions that evaluated to TRUE. */
extern volatile uint32 g_hv_vuart_test_assert_passed;

/** Number of assertions that evaluated to FALSE. */
extern volatile uint32 g_hv_vuart_test_assert_failed;

/** Assert ID of the most recent failed assertion (valid when failed > 0). */
extern volatile uint32 g_hv_vuart_test_last_failed_id;

/** Bitmask of hit-point IDs that have fired at least once. */
extern volatile uint32 g_hv_vuart_test_hit_mask;

/* -------------------------------------------------------------------------
 * Hit point identifiers
 *
 * One constant per observable code location.  The numeric value must be
 * less than 32 so that it can be encoded as a bit in g_hv_vuart_test_hit_mask.
 *-------------------------------------------------------------------------*/

/** Entered VUart_Cfg_Init. */
#define HV_VUART_HIT_CFG_INIT_ENTER                0u
/** VUart_Cfg_Init completed without error. */
#define HV_VUART_HIT_CFG_INIT_OK                   1u
/** Entered VUart_Init on this core. */
#define HV_VUART_HIT_INIT_ENTER                    2u
/** VUart_Init: physical UART initialized (owner core branch). */
#define HV_VUART_HIT_INIT_PHYS_UART                3u
/** VUart_Init: at least one instance cleared. */
#define HV_VUART_HIT_INIT_INSTANCE_CLEARED         4u
/** Entered VUart_WriteReg. */
#define HV_VUART_HIT_WRITE_REG_ENTER               5u
/** VUart_WriteReg: DR case reached. */
#define HV_VUART_HIT_WRITE_REG_DR                  6u
/** VUart_WriteReg: CR case reached. */
#define HV_VUART_HIT_WRITE_REG_CR                  7u
/** VUart_WriteReg: DST_ID case reached. */
#define HV_VUART_HIT_WRITE_REG_DST_ID              8u
/** Entered VUart_ReadReg. */
#define HV_VUART_HIT_READ_REG_ENTER                9u
/** VUart_ReadReg: DR case reached. */
#define HV_VUART_HIT_READ_REG_DR                   10u
/** VUart_ReadReg: SR case reached. */
#define HV_VUART_HIT_READ_REG_SR                   11u
/** VUart_FrameParser_Feed: frame completed and dispatched. */
#define HV_VUART_HIT_PARSER_FRAME_COMPLETE         12u
/** VUart_FrameParser_Feed: parser reset due to invalid byte. */
#define HV_VUART_HIT_PARSER_RESET                  13u
/** VUart_DispatchFrame: local delivery path taken. */
#define HV_VUART_HIT_DISPATCH_LOCAL                14u
/** VUart_DispatchFrame: IPC/cross-core path taken. */
#define HV_VUART_HIT_DISPATCH_IPC                  15u
/** VUart_DispatchFrame: frame discarded (invalid destination). */
#define HV_VUART_HIT_DISPATCH_DISCARD              16u
/** VUart_RxRingPush: overflow detected. */
#define HV_VUART_HIT_RX_OVERFLOW                   17u
/** VUart_TxQueuePush: queue full detected. */
#define HV_VUART_HIT_TX_FULL                       18u
/** VUart_IpcPushFrame: ring full detected. */
#define HV_VUART_HIT_IPC_PUSH_FULL                 19u
/** VUart_InjectVirtualIrq called. */
#define HV_VUART_HIT_VIRTUAL_IRQ                   20u
/** VUart_IpiRxHandler entered. */
#define HV_VUART_HIT_IPI_RX_ENTER                  21u
/** VUart_RxIsr entered. */
#define HV_VUART_HIT_RX_ISR_ENTER                  22u
/** VUart_TxKick called. */
#define HV_VUART_HIT_TX_KICK                       23u
/** VUart_Cfg_GetByCoreId called with invalid core id. */
#define HV_VUART_HIT_CFG_GET_INVALID_CORE          24u
/** VUart_FindLocalInstanceByVmId returned NULL_PTR. */
#define HV_VUART_HIT_FIND_INSTANCE_NOT_FOUND       25u
/* IDs 26-31 reserved for future use. */

/* -------------------------------------------------------------------------
 * API
 *-------------------------------------------------------------------------*/

/**
 * @brief Reset all test state variables to zero / scenario default.
 *
 * Call once at the beginning of each test scenario before exercising any
 * VirtualUART function under test.
 */
void HvVUartTest_Reset(void);

/**
 * @brief Record that a named hit-point has been reached.
 *
 * @param hit_id  One of the HV_VUART_HIT_xxx constants (must be < 32).
 */
void HvVUartTest_Hit(uint32 hit_id);

/**
 * @brief Evaluate a boolean condition and record PASS / FAIL.
 *
 * @param assert_id  Caller-assigned identifier stored on failure.
 * @param condition  TRUE (non-zero) means the assertion passes.
 */
void HvVUartTest_AssertTrue(uint32 assert_id, boolean condition);

/**
 * @brief Evaluate equality of two uint32 values and record PASS / FAIL.
 *
 * @param assert_id  Caller-assigned identifier stored on failure.
 * @param expected   Expected value.
 * @param actual     Observed value.
 */
void HvVUartTest_AssertEqU32(uint32 assert_id, uint32 expected, uint32 actual);

/* -------------------------------------------------------------------------
 * Production-code instrumentation macros
 *
 * These macros are placed inside VirtualUART implementation files.  They
 * compile to zero cost when testing is disabled.
 *-------------------------------------------------------------------------*/

#define HV_VUART_TEST_HIT(hit_id) \
    HvVUartTest_Hit((uint32)(hit_id))

#define HV_VUART_TEST_ASSERT_TRUE(assert_id, condition) \
    HvVUartTest_AssertTrue((uint32)(assert_id), (boolean)(condition))

#define HV_VUART_TEST_ASSERT_EQ_U32(assert_id, expected, actual) \
    HvVUartTest_AssertEqU32((uint32)(assert_id), (uint32)(expected), (uint32)(actual))

#else /* testing disabled -------------------------------------------------- */

#define HV_VUART_TEST_HIT(hit_id)                            ((void)0)
#define HV_VUART_TEST_ASSERT_TRUE(assert_id, condition)      ((void)0)
#define HV_VUART_TEST_ASSERT_EQ_U32(id, exp, act)            ((void)0)

#endif /* (HV_TEST_ENABLE != 0u) && (HV_VUART_TEST_ENABLE != 0u) */

#endif /* HV_VUART_TEST_H_ */
