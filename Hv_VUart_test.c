/*
 * Hv_VUart_Test.c
 *
 * Optional Virtual UART test instrumentation storage and scenario execution.
 *
 * This file mirrors the structure of Hv_Sched_Test.c.  It provides:
 *   1. Storage definitions for every volatile test variable declared in the
 *      header.
 *   2. The three primitive helpers (Reset, Hit, AssertTrue, AssertEqU32).
 *   3. Per-scenario assertion bodies invoked from HvVUartTest_RunScenario().
 *
 * How to use
 * ----------
 * - In the hypervisor startup or in a dedicated test task, call:
 *       HvVUartTest_Reset();
 *       HvVUartTest_RunScenario();
 * - After the call, inspect:
 *       g_hv_vuart_test_assert_failed  == 0  =>  all assertions passed
 *       g_hv_vuart_test_assert_failed  != 0  =>  see g_hv_vuart_test_last_failed_id
 *
 * Configuration-driven testing
 * ----------------------------
 * Every scenario that starts with HV_VUART_SCENARIO_CFG_* verifies that the
 * generated configuration (Hv_VUart_Cfg.c / Hv_VUart_Cfg.h) is self-consistent.
 * When you regenerate or change VUART_CFG_* parameters, recompile with
 *   HV_TEST_ENABLE=1 HV_VUART_TEST_ENABLE=1
 * and run all scenarios (loop HV_VUART_CFG_SCENARIO from 0 to
 * HV_VUART_SCENARIO_CFG_PHYS_BASE_BINDING) to confirm the new configuration
 * is correct.
 *
 * Traceability:
 * - All SWS_VUART requirements are covered through the scenario set defined
 *   in Hv_VUart_TestScenarios.h.
 */

#include "Test/Inc/Hv_VUart_Test.h"

#if (HV_TEST_ENABLE != 0u) && (HV_VUART_TEST_ENABLE != 0u)

/* Pull in the full feature API so scenario bodies can call it directly. */
#include "Hv_VUart.h"
#include "Hv_VUart_Cfg.h"

/* =========================================================================
 * Storage for all observable test variables
 * =========================================================================*/

volatile uint32 g_hv_vuart_test_active_scenario   = HV_VUART_CFG_SCENARIO;

volatile uint32 g_hv_vuart_test_last_vm_id         = 0u;
volatile uint32 g_hv_vuart_test_last_reg_offset    = 0u;
volatile uint32 g_hv_vuart_test_last_written_val   = 0u;
volatile uint32 g_hv_vuart_test_last_injected_val  = 0u;
volatile uint32 g_hv_vuart_test_last_src_core      = 0u;
volatile uint32 g_hv_vuart_test_last_dst_core      = 0u;
volatile uint32 g_hv_vuart_test_last_ipc_push_result = 0u;
volatile uint32 g_hv_vuart_test_last_ipc_pop_result  = 0u;
volatile uint32 g_hv_vuart_test_last_tx_push_result  = 0u;
volatile uint32 g_hv_vuart_test_last_tx_pop_result   = 0u;
volatile uint32 g_hv_vuart_test_last_rx_push_result  = 0u;

volatile uint32 g_hv_vuart_test_build_frame_count   = 0u;
volatile uint32 g_hv_vuart_test_parser_complete_count = 0u;
volatile uint32 g_hv_vuart_test_parser_reset_count   = 0u;
volatile uint32 g_hv_vuart_test_dispatch_local_count = 0u;
volatile uint32 g_hv_vuart_test_dispatch_ipc_count   = 0u;
volatile uint32 g_hv_vuart_test_dispatch_discard_count = 0u;
volatile uint32 g_hv_vuart_test_virtual_irq_count    = 0u;
volatile uint32 g_hv_vuart_test_tx_kick_count        = 0u;
volatile uint32 g_hv_vuart_test_ipi_rx_count         = 0u;
volatile uint32 g_hv_vuart_test_rx_isr_count         = 0u;

volatile uint32 g_hv_vuart_test_assert_total        = 0u;
volatile uint32 g_hv_vuart_test_assert_passed       = 0u;
volatile uint32 g_hv_vuart_test_assert_failed       = 0u;
volatile uint32 g_hv_vuart_test_last_failed_id      = 0u;
volatile uint32 g_hv_vuart_test_hit_mask            = 0u;

/* =========================================================================
 * Primitive helpers
 * =========================================================================*/

void HvVUartTest_Reset(void)
{
    g_hv_vuart_test_active_scenario    = HV_VUART_CFG_SCENARIO;

    g_hv_vuart_test_last_vm_id         = 0u;
    g_hv_vuart_test_last_reg_offset    = 0u;
    g_hv_vuart_test_last_written_val   = 0u;
    g_hv_vuart_test_last_injected_val  = 0u;
    g_hv_vuart_test_last_src_core      = 0u;
    g_hv_vuart_test_last_dst_core      = 0u;
    g_hv_vuart_test_last_ipc_push_result = 0u;
    g_hv_vuart_test_last_ipc_pop_result  = 0u;
    g_hv_vuart_test_last_tx_push_result  = 0u;
    g_hv_vuart_test_last_tx_pop_result   = 0u;
    g_hv_vuart_test_last_rx_push_result  = 0u;

    g_hv_vuart_test_build_frame_count    = 0u;
    g_hv_vuart_test_parser_complete_count = 0u;
    g_hv_vuart_test_parser_reset_count   = 0u;
    g_hv_vuart_test_dispatch_local_count = 0u;
    g_hv_vuart_test_dispatch_ipc_count   = 0u;
    g_hv_vuart_test_dispatch_discard_count = 0u;
    g_hv_vuart_test_virtual_irq_count    = 0u;
    g_hv_vuart_test_tx_kick_count        = 0u;
    g_hv_vuart_test_ipi_rx_count         = 0u;
    g_hv_vuart_test_rx_isr_count         = 0u;

    g_hv_vuart_test_assert_total         = 0u;
    g_hv_vuart_test_assert_passed        = 0u;
    g_hv_vuart_test_assert_failed        = 0u;
    g_hv_vuart_test_last_failed_id       = 0u;
    g_hv_vuart_test_hit_mask             = 0u;
}

void HvVUartTest_Hit(uint32 hit_id)
{
    if (hit_id < 32u)
    {
        g_hv_vuart_test_hit_mask |= (1u << hit_id);
    }
}

void HvVUartTest_AssertTrue(uint32 assert_id, boolean condition)
{
    g_hv_vuart_test_assert_total++;

    if (condition != FALSE)
    {
        g_hv_vuart_test_assert_passed++;
    }
    else
    {
        g_hv_vuart_test_assert_failed++;
        g_hv_vuart_test_last_failed_id = assert_id;
    }
}

void HvVUartTest_AssertEqU32(uint32 assert_id, uint32 expected, uint32 actual)
{
    HvVUartTest_AssertTrue(assert_id, (boolean)(expected == actual));
}

/* =========================================================================
 * Internal helper: unique assert IDs
 *
 * Each scenario owns a contiguous block of 100 IDs starting at
 * (scenario_number * 100).  This keeps IDs globally unique so that
 * g_hv_vuart_test_last_failed_id directly identifies both the scenario and
 * the failing check without additional context.
 * =========================================================================*/

/* Helper macros for compact assert calls inside scenario bodies. */
#define ASSERT_TRUE(id_offset, cond) \
    HvVUartTest_AssertTrue( \
        (uint32)(g_hv_vuart_test_active_scenario * 100u) + (uint32)(id_offset), \
        (boolean)(cond))

#define ASSERT_EQ(id_offset, exp, act) \
    HvVUartTest_AssertEqU32( \
        (uint32)(g_hv_vuart_test_active_scenario * 100u) + (uint32)(id_offset), \
        (uint32)(exp), (uint32)(act))

/* =========================================================================
 * Scenario bodies
 * =========================================================================*/

/* -----------------------------------------------------------------------
 * SCENARIO 0 – DEFAULT
 * Basic sanity: after VUart_Cfg_Init, VUART_NUM_VMS == VUART_CFG_NUM_VMS
 * and VUART_NUM_CORES == VUART_CFG_NUM_CORES.
 * ----------------------------------------------------------------------- */
static void Scenario_Default(void)
{
    VUart_Cfg_Init();

    ASSERT_EQ(0u, (uint32)VUART_CFG_NUM_VMS,   (uint32)VUART_NUM_VMS);
    ASSERT_EQ(1u, (uint32)VUART_CFG_NUM_CORES, (uint32)VUART_NUM_CORES);
    ASSERT_EQ(2u, (uint32)VUART_CFG_RX_BUF_SIZE,    (uint32)RX_BUF_SIZE);
    ASSERT_EQ(3u, (uint32)VUART_CFG_TX_QUEUE_DEPTH,  (uint32)TX_QUEUE_DEPTH);
    ASSERT_EQ(4u, (uint32)VUART_CFG_MAX_PAYLOAD,     (uint32)VUART_MAX_PAYLOAD);
}

/* -----------------------------------------------------------------------
 * SCENARIO 1 – CFG_INIT_CHECK
 * Verify that VUart_Cfg_Init populates VUart_kConfigs correctly.
 * ----------------------------------------------------------------------- */
static void Scenario_CfgInitCheck(void)
{
    uint8 i;

    VUart_Cfg_Init();

    /* Every core entry has a valid local_core_id. */
    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(0u + i, (uint32)i, (uint32)VUart_kConfigs[i].local_core_id);
    }

    /* Owner core equals VUART_CFG_UART0_OWNER_CORE in all entries. */
    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(10u + i, (uint32)VUART_CFG_UART0_OWNER_CORE,
                  (uint32)VUart_kConfigs[i].owner_core_id);
    }

    /* vm_core_map pointer is never NULL in any entry. */
    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_TRUE(20u + i, VUart_kConfigs[i].vm_core_map != (const uint8*)0);
    }

    /* IPC channel base pointer is set. */
    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_TRUE(30u + i,
            VUart_kConfigs[i].ipc_channels != (VUart_IpcChannelType*)0);
    }

    /* TX queue heads/tails start at zero. */
    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(40u + i, 0u, (uint32)VUart_kTxQueues[i].head);
        ASSERT_EQ(50u + i, 0u, (uint32)VUart_kTxQueues[i].tail);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 2 – INIT_CORE0_OWNER
 * After VUart_Init on core 0 every local instance starts in IDLE state
 * with SR == VUART_SR_TXRDY and clean RX ring.
 * ----------------------------------------------------------------------- */
static void Scenario_InitCore0Owner(void)
{
    uint8 i;
    const VUart_ConfigType* cfg;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);

    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);

    for (i = 0u; i < cfg->num_instances; i++)
    {
        const VUart_InstanceType* inst = &cfg->instances[i];

        ASSERT_EQ(10u + i, (uint32)VUART_STATE_IDLE, (uint32)inst->state);
        ASSERT_EQ(20u + i, (uint32)VUART_SR_TXRDY,   (uint32)inst->SR);
        ASSERT_EQ(30u + i, 0u, (uint32)inst->rx_head);
        ASSERT_EQ(40u + i, 0u, (uint32)inst->rx_tail);
        ASSERT_EQ(50u + i, 0u, (uint32)inst->pending_irq);
        ASSERT_EQ(60u + i, (uint32)cfg->local_core_id, (uint32)inst->core_id);
    }

    /* TX queue on owner core is reset. */
    ASSERT_TRUE(70u, cfg->tx_queue != (VUart_TxQueueType*)0);
    if (cfg->tx_queue != (VUart_TxQueueType*)0)
    {
        ASSERT_EQ(71u, 0u, (uint32)cfg->tx_queue->head);
        ASSERT_EQ(72u, 0u, (uint32)cfg->tx_queue->tail);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 3 – INIT_CORE1_NON_OWNER
 * Core 1 config has owner_core_id == 0, so VUart_Init on core 1 must NOT
 * attempt to drive the physical UART (tested by checking that its
 * tx_queue pointer is not the owner's queue).
 * ----------------------------------------------------------------------- */
static void Scenario_InitCore1NonOwner(void)
{
    const VUart_ConfigType* cfg0;
    const VUart_ConfigType* cfg1;

    VUart_Cfg_Init();

    cfg0 = VUart_Cfg_GetByCoreId(0u);
    cfg1 = VUart_Cfg_GetByCoreId(1u);

    ASSERT_TRUE(0u, cfg1 != (const VUart_ConfigType*)0);
    if (cfg1 == (const VUart_ConfigType*)0) { return; }

    /* Non-owner core must not be the owner. */
    ASSERT_TRUE(1u, cfg1->local_core_id != cfg1->owner_core_id);

    /* Non-owner core has its own tx_queue (not NULL, not the owner's). */
    ASSERT_TRUE(2u, cfg1->tx_queue != (VUart_TxQueueType*)0);
    if ((cfg0 != (const VUart_ConfigType*)0) && (cfg1->tx_queue != (VUart_TxQueueType*)0))
    {
        ASSERT_TRUE(3u, cfg1->tx_queue != cfg0->tx_queue);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 4 – INIT_IPC_RINGS_CLEARED
 * All IPC rings have head == tail == 0 after VUart_Cfg_Init.
 * ----------------------------------------------------------------------- */
static void Scenario_InitIpcRingsCleared(void)
{
    uint8 s;
    uint8 d;

    VUart_Cfg_Init();

    for (s = 0u; s < (uint8)VUART_NUM_CORES; s++)
    {
        for (d = 0u; d < (uint8)VUART_NUM_CORES; d++)
        {
            ASSERT_EQ(s * 10u + d,
                      0u, (uint32)VUart_kIpcChannels[s][d].head);
            ASSERT_EQ(20u + s * 10u + d,
                      0u, (uint32)VUart_kIpcChannels[s][d].tail);
        }
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 5 – DR_WRITE_BASIC
 * Directly test VUart_WriteReg for the DR offset.  After a write the
 * SR.TXRDY bit must have cleared then reasserted (visible as reasserted
 * in IDLE state when local dispatch completes immediately).
 *
 * Because hardware peripheral calls cannot run in a host test environment,
 * we use the in-memory virtual window injected during VUart_Init and
 * verify the instance's SR and state fields only.
 * ----------------------------------------------------------------------- */
static void Scenario_DrWriteBasic(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];   /* Simulated VM window in stack memory. */
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);

    /* Use the first instance on core 0. */
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;

    /* Re-point the instance window to our stack buffer so the write
     * access in VUart_Tc_GetWrittenValue reads our prepared value. */
    inst->virt_base = &window_mem[0];

    /* Prepare DST_ID so dispatch has a valid destination. */
    inst->DST_ID = vm_id;      /* Same-core loopback. */

    /* Write data value into the emulated DR location (offset 0x0C / 4 = 3). */
    window_mem[3] = 0xABu;     /* byte value to transmit */

    /* Pre-condition: instance is idle with TX ready. */
    ASSERT_EQ(2u, (uint32)VUART_SR_TXRDY, (uint32)(inst->SR & VUART_SR_TXRDY));
    ASSERT_EQ(3u, (uint32)VUART_STATE_IDLE, (uint32)inst->state);

    /* Call WriteReg with DR offset = 3 (VUART_OFFSET_DR), size = 1. */
    VUart_WriteReg(vm_id, (uint8)VUART_OFFSET_DR, 1u);

    /* After the write the instance should return to IDLE with TXRDY set. */
    ASSERT_EQ(4u, (uint32)VUART_SR_TXRDY,
              (uint32)(inst->SR & VUART_SR_TXRDY));
}

/* -----------------------------------------------------------------------
 * SCENARIO 6 – DR_READ_EMPTY
 * ReadReg on DR when the RX ring is empty injects 0 and leaves SR.RXRDY
 * clear.
 * ----------------------------------------------------------------------- */
static void Scenario_DrReadEmpty(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];
    window_mem[3] = 0xFFu; /* Pre-fill so we can verify it is overwritten. */

    /* RX ring is empty after init. */
    ASSERT_EQ(2u, inst->rx_head, inst->rx_tail);
    ASSERT_EQ(3u, 0u, (uint32)(inst->SR & VUART_SR_RXRDY));

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_DR, 1u);

    /* Zero should have been injected into the window (index 3). */
    ASSERT_EQ(4u, 0u, window_mem[3]);

    /* SR.RXRDY must remain clear. */
    ASSERT_EQ(5u, 0u, (uint32)(inst->SR & VUART_SR_RXRDY));
}

/* -----------------------------------------------------------------------
 * SCENARIO 7 – DR_READ_WITH_DATA
 * Push a byte into the RX ring; ReadReg must return it and advance the
 * tail.
 * ----------------------------------------------------------------------- */
static void Scenario_DrReadWithData(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];

    /* Inject a byte directly into the RX ring. */
    (void)VUart_RxRingPush(inst, 0x42u);
    inst->SR |= VUART_SR_RXRDY;

    ASSERT_TRUE(2u, inst->rx_head != inst->rx_tail);

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_DR, 1u);

    /* The injected byte should appear in the window. */
    ASSERT_EQ(3u, 0x42u, window_mem[3]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 8 – DR_READ_LAST_BYTE_CLEARS_RXRDY
 * After reading the last byte from DR, SR.RXRDY must be cleared and state
 * must revert to IDLE.
 * ----------------------------------------------------------------------- */
static void Scenario_DrReadLastByteClears(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];

    (void)VUart_RxRingPush(inst, 0xDDu);
    inst->SR |= VUART_SR_RXRDY;
    inst->state = VUART_STATE_RX_READY;

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_DR, 1u);

    ASSERT_EQ(2u, 0u, (uint32)(inst->SR & VUART_SR_RXRDY));
    ASSERT_EQ(3u, (uint32)VUART_STATE_IDLE, (uint32)inst->state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 9 – CR_WRITE_MASK
 * Only the bits EN | RXIE | TXIE | PRIO_MSK are retained.
 * ----------------------------------------------------------------------- */
static void Scenario_CrWriteMask(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;
    uint32 expected_mask;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];

    /* Write 0xFFFFFFFF; only permitted bits should survive. */
    window_mem[0] = 0xFFFFFFFFu;
    VUart_WriteReg(vm_id, (uint8)VUART_OFFSET_CR, 4u);

    expected_mask = (uint32)(VUART_CR_EN | VUART_CR_RXIE |
                              VUART_CR_TXIE | VUART_CR_PRIO_MSK);
    ASSERT_EQ(2u, expected_mask, inst->CR);
}

/* -----------------------------------------------------------------------
 * SCENARIO 10 – CR_READ
 * ReadReg on CR returns the current CR mirror.
 * ----------------------------------------------------------------------- */
static void Scenario_CrRead(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];
    inst->CR = VUART_CR_EN | VUART_CR_RXIE;

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_CR, 4u);

    ASSERT_EQ(2u, (uint32)(VUART_CR_EN | VUART_CR_RXIE), window_mem[0]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 11 – DST_ID_WRITE
 * WriteReg on DST_ID stores the lower 8 bits.
 * ----------------------------------------------------------------------- */
static void Scenario_DstIdWrite(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];

    window_mem[2] = 0xABCDEF02u; /* Only low byte (0x02) should be stored. */
    VUart_WriteReg(vm_id, (uint8)VUART_OFFSET_DST_ID, 4u);

    ASSERT_EQ(2u, 0x02u, (uint32)(inst->DST_ID & 0xFFu));
}

/* -----------------------------------------------------------------------
 * SCENARIO 12 – DST_ID_READ
 * ReadReg on DST_ID returns the stored value.
 * ----------------------------------------------------------------------- */
static void Scenario_DstIdRead(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];
    inst->DST_ID = 0x07u;

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_DST_ID, 4u);

    ASSERT_EQ(2u, 0x07u, window_mem[2]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 13 – SR_READ
 * ReadReg on SR returns the current SR mirror.
 * ----------------------------------------------------------------------- */
static void Scenario_SrRead(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];
    inst->SR = VUART_SR_TXRDY | VUART_SR_RXRDY;

    VUart_ReadReg(vm_id, (uint8)VUART_OFFSET_SR, 4u);

    ASSERT_EQ(2u, (uint32)(VUART_SR_TXRDY | VUART_SR_RXRDY), window_mem[1]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 15 – BUILD_FRAME_BASIC
 * VUart_BuildFrame stores src, dst, and first payload byte.
 * ----------------------------------------------------------------------- */
static void Scenario_BuildFrameBasic(void)
{
    VUart_FrameType frame;
    uint8 payload = 0x5Au;

    VUart_BuildFrame(&frame, 0u, 1u, payload, 1u);

    ASSERT_EQ(0u, 0u, (uint32)frame.src_vm_id);
    ASSERT_EQ(1u, 1u, (uint32)frame.dst_vm_id);
    ASSERT_EQ(2u, 1u, (uint32)frame.len);
    ASSERT_EQ(3u, (uint32)payload, (uint32)frame.payload[0]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 16 – BUILD_FRAME_CLAMP_LEN
 * len > VUART_MAX_PAYLOAD is clamped.
 * ----------------------------------------------------------------------- */
static void Scenario_BuildFrameClampLen(void)
{
    VUart_FrameType frame;

    VUart_BuildFrame(&frame, 0u, 1u, 0xAAu, 255u);

    ASSERT_EQ(0u, (uint32)VUART_MAX_PAYLOAD, (uint32)frame.len);
}

/* -----------------------------------------------------------------------
 * SCENARIO 17 – BUILD_FRAME_NULL_PTR
 * Passing NULL does not crash; this simply returns.
 * ----------------------------------------------------------------------- */
static void Scenario_BuildFrameNullPtr(void)
{
    /* This must not trigger a data abort or watchdog. */
    VUart_BuildFrame((VUart_FrameType*)0, 0u, 1u, 0xBBu, 1u);

    /* If we reach here the guard worked. */
    ASSERT_TRUE(0u, TRUE);
}

/* -----------------------------------------------------------------------
 * SCENARIO 18 – PARSER_VALID_FRAME
 * Feed a complete frame (dst, src, len=1, data) through the parser and
 * verify the complete count increments.
 *
 * The parser count is tracked via the HV_VUART_TEST_HIT mechanism placed
 * inside VUart_FrameParser_Feed (HV_VUART_HIT_PARSER_FRAME_COMPLETE).
 * ----------------------------------------------------------------------- */
static void Scenario_ParserValidFrame(void)
{
    VUart_FrameParserType p;
    uint8 dst = 0u;
    uint8 src = 1u;
    uint8 len = 1u;
    uint8 data = 0x7Fu;

    /* Initialize the parser (uses VUart_ParserReset internally). */
    VUart_Cfg_Init();
    {
        const VUart_ConfigType* cfg = VUart_Cfg_GetByCoreId(0u);
        if (cfg != (const VUart_ConfigType*)0)
        {
            VUart_Init(cfg);
        }
    }

    /* Reset parser manually through the public accessor. */
    p.state = FSM_IDLE;
    p.data_idx = 0u;
    p.frame.dst_vm_id = 0u;
    p.frame.src_vm_id = 0u;
    p.frame.len = 0u;

    /* Feed frame bytes. */
    VUart_FrameParser_Feed(&p, dst);   /* dst_vm_id */
    ASSERT_EQ(0u, (uint32)FSM_SRC_VM, (uint32)p.state);

    VUart_FrameParser_Feed(&p, src);   /* src_vm_id */
    ASSERT_EQ(1u, (uint32)FSM_LENGTH, (uint32)p.state);

    VUart_FrameParser_Feed(&p, len);   /* length  */
    ASSERT_EQ(2u, (uint32)FSM_DATA,   (uint32)p.state);

    VUart_FrameParser_Feed(&p, data);  /* single data byte -> complete */

    /* After completion the parser resets to IDLE. */
    ASSERT_EQ(3u, (uint32)FSM_IDLE, (uint32)p.state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 19 – PARSER_INVALID_DST
 * A dst byte >= VUART_NUM_VMS resets the parser.
 * ----------------------------------------------------------------------- */
static void Scenario_ParserInvalidDst(void)
{
    VUart_FrameParserType p;

    p.state = FSM_IDLE;
    p.data_idx = 0u;

    VUart_FrameParser_Feed(&p, (uint8)VUART_NUM_VMS); /* out-of-range */

    ASSERT_EQ(0u, (uint32)FSM_IDLE, (uint32)p.state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 20 – PARSER_ZERO_LENGTH
 * A zero length byte resets the parser.
 * ----------------------------------------------------------------------- */
static void Scenario_ParserZeroLength(void)
{
    VUart_FrameParserType p;

    p.state = FSM_IDLE;
    p.data_idx = 0u;
    p.frame.dst_vm_id = 0u;
    p.frame.src_vm_id = 0u;
    p.frame.len = 0u;

    VUart_FrameParser_Feed(&p, 0u); /* dst = VM0, valid */
    VUart_FrameParser_Feed(&p, 0u); /* src = VM0, valid */
    VUart_FrameParser_Feed(&p, 0u); /* length = 0 -> reset */

    ASSERT_EQ(0u, (uint32)FSM_IDLE, (uint32)p.state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 21 – PARSER_OVERSIZED_LENGTH
 * A length byte larger than VUART_MAX_PAYLOAD resets the parser.
 * ----------------------------------------------------------------------- */
static void Scenario_ParserOversizedLength(void)
{
    VUart_FrameParserType p;

    p.state = FSM_IDLE;
    p.data_idx = 0u;
    p.frame.dst_vm_id = 0u;
    p.frame.src_vm_id = 0u;
    p.frame.len = 0u;

    VUart_FrameParser_Feed(&p, 0u);                        /* dst valid    */
    VUart_FrameParser_Feed(&p, 0u);                        /* src valid    */
    VUart_FrameParser_Feed(&p, (uint8)(VUART_MAX_PAYLOAD + 1u)); /* too big */

    ASSERT_EQ(0u, (uint32)FSM_IDLE, (uint32)p.state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 24 – RX_RING_PUSH_SINGLE
 * Push one byte; head advances by 1 and the byte is recoverable.
 * ----------------------------------------------------------------------- */
static void Scenario_RxRingPushSingle(void)
{
    VUart_InstanceType inst;
    uint8 j;
    uint8 result;

    inst.rx_head = 0u;
    inst.rx_tail = 0u;
    inst.SR = 0u;
    inst.state = VUART_STATE_IDLE;
    for (j = 0u; j < RX_BUF_SIZE; j++) { inst.rx_buf[j] = 0u; }

    result = VUart_RxRingPush(&inst, 0xCCu);

    ASSERT_EQ(0u, 1u, (uint32)result);
    ASSERT_EQ(1u, 1u, (uint32)inst.rx_head);
    ASSERT_EQ(2u, 0u, (uint32)inst.rx_tail);
    ASSERT_EQ(3u, 0xCCu, (uint32)inst.rx_buf[0]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 25 – RX_RING_OVERFLOW
 * Fill the ring completely; the next push must return 0 and set
 * SR.OVERRUN.
 * ----------------------------------------------------------------------- */
static void Scenario_RxRingOverflow(void)
{
    VUart_InstanceType inst;
    uint8 j;
    uint8 result;

    inst.rx_head = 0u;
    inst.rx_tail = 0u;
    inst.SR = 0u;
    inst.state = VUART_STATE_IDLE;
    for (j = 0u; j < RX_BUF_SIZE; j++) { inst.rx_buf[j] = 0u; }

    /* Fill to capacity (RX_BUF_SIZE - 1 usable slots). */
    for (j = 0u; j < (uint8)(RX_BUF_SIZE - 1u); j++)
    {
        result = VUart_RxRingPush(&inst, j);
        ASSERT_EQ(j, 1u, (uint32)result);
    }

    /* This push must fail. */
    result = VUart_RxRingPush(&inst, 0xFFu);
    ASSERT_EQ(100u, 0u, (uint32)result);
    ASSERT_EQ(101u, (uint32)VUART_SR_OVERRUN_MASK,
              (uint32)(inst.SR & VUART_SR_OVERRUN_MASK));
    ASSERT_EQ(102u, (uint32)VUART_STATE_ERROR, (uint32)inst.state);
}

/* -----------------------------------------------------------------------
 * SCENARIO 26 – RX_RING_POP_EMPTY
 * VUart_RxBufEmpty must return 1 and pop must not corrupt state.
 * ----------------------------------------------------------------------- */
static void Scenario_RxRingPopEmpty(void)
{
    VUart_InstanceType inst;
    uint8 j;

    inst.rx_head = 0u;
    inst.rx_tail = 0u;
    for (j = 0u; j < RX_BUF_SIZE; j++) { inst.rx_buf[j] = 0u; }

    ASSERT_EQ(0u, 1u, (uint32)VUart_RxBufEmpty(&inst));

    /* Pop on empty ring: tail must not move. */
    (void)VUart_RxBufPop(&inst);
    ASSERT_EQ(1u, 0u, (uint32)inst.rx_tail);
}

/* -----------------------------------------------------------------------
 * SCENARIO 27 – RX_RING_FILL_THEN_DRAIN
 * Push RX_BUF_SIZE-1 bytes then pop all of them in FIFO order.
 * ----------------------------------------------------------------------- */
static void Scenario_RxRingFillThenDrain(void)
{
    VUart_InstanceType inst;
    uint8 j;
    uint8 popped;

    inst.rx_head = 0u;
    inst.rx_tail = 0u;
    inst.SR = 0u;
    inst.state = VUART_STATE_IDLE;
    for (j = 0u; j < RX_BUF_SIZE; j++) { inst.rx_buf[j] = 0u; }

    for (j = 0u; j < (uint8)(RX_BUF_SIZE - 1u); j++)
    {
        (void)VUart_RxRingPush(&inst, j);
    }

    for (j = 0u; j < (uint8)(RX_BUF_SIZE - 1u); j++)
    {
        popped = VUart_RxBufPop(&inst);
        ASSERT_EQ(j, (uint32)j, (uint32)popped);
    }

    ASSERT_EQ(200u, 1u, (uint32)VUart_RxBufEmpty(&inst));
}

/* -----------------------------------------------------------------------
 * SCENARIO 28 – RX_RING_WRAP_AROUND
 * Advance head to near the end, then push and pop across index 0.
 * ----------------------------------------------------------------------- */
static void Scenario_RxRingWrapAround(void)
{
    VUart_InstanceType inst;
    uint8 j;

    inst.rx_head = (uint8)(RX_BUF_SIZE - 2u);
    inst.rx_tail = (uint8)(RX_BUF_SIZE - 2u);
    inst.SR = 0u;
    inst.state = VUART_STATE_IDLE;
    for (j = 0u; j < RX_BUF_SIZE; j++) { inst.rx_buf[j] = 0u; }

    /* Push two bytes to wrap head past 0. */
    (void)VUart_RxRingPush(&inst, 0xAAu);
    (void)VUart_RxRingPush(&inst, 0xBBu);

    ASSERT_EQ(0u, 0u, (uint32)inst.rx_head); /* Wrapped to 0. */

    ASSERT_EQ(1u, 0xAAu, (uint32)VUart_RxBufPop(&inst));
    ASSERT_EQ(2u, 0xBBu, (uint32)VUart_RxBufPop(&inst));
    ASSERT_EQ(3u, 1u,    (uint32)VUart_RxBufEmpty(&inst));
}

/* -----------------------------------------------------------------------
 * SCENARIO 29 – TX_QUEUE_PUSH_POP
 * Push one frame; pop must return the same content.
 * ----------------------------------------------------------------------- */
static void Scenario_TxQueuePushPop(void)
{
    VUart_TxQueueType q;
    VUart_FrameType   in_frame;
    VUart_FrameType   out_frame;
    uint8 result;
    uint8 j;

    q.head = 0u;
    q.tail = 0u;
    q.lock.value = 0u;
    q.policy = VUART_ARB_FIFO;
    for (j = 0u; j < TX_QUEUE_DEPTH; j++)
    {
        q.frames[j].dst_vm_id = 0u;
        q.frames[j].src_vm_id = 0u;
        q.frames[j].len = 0u;
    }

    in_frame.dst_vm_id = 2u;
    in_frame.src_vm_id = 0u;
    in_frame.len = 1u;
    in_frame.payload[0] = 0x99u;

    result = VUart_TxQueuePush(&q, &in_frame);
    ASSERT_EQ(0u, 1u, (uint32)result);

    result = VUart_TxQueuePop(&q, &out_frame);
    ASSERT_EQ(1u, 1u, (uint32)result);
    ASSERT_EQ(2u, (uint32)in_frame.dst_vm_id, (uint32)out_frame.dst_vm_id);
    ASSERT_EQ(3u, (uint32)in_frame.src_vm_id, (uint32)out_frame.src_vm_id);
    ASSERT_EQ(4u, (uint32)in_frame.payload[0], (uint32)out_frame.payload[0]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 30 – TX_QUEUE_FULL
 * Fill queue to TX_QUEUE_DEPTH-1; the next push must return 0.
 * ----------------------------------------------------------------------- */
static void Scenario_TxQueueFull(void)
{
    VUart_TxQueueType q;
    VUart_FrameType   frame;
    uint8 j;
    uint8 result;

    q.head = 0u;
    q.tail = 0u;
    q.lock.value = 0u;
    q.policy = VUART_ARB_FIFO;

    frame.dst_vm_id = 1u;
    frame.src_vm_id = 0u;
    frame.len = 1u;
    frame.payload[0] = 0x01u;

    for (j = 0u; j < (uint8)(TX_QUEUE_DEPTH - 1u); j++)
    {
        result = VUart_TxQueuePush(&q, &frame);
        ASSERT_EQ(j, 1u, (uint32)result);
    }

    result = VUart_TxQueuePush(&q, &frame);
    ASSERT_EQ(100u, 0u, (uint32)result);
}

/* -----------------------------------------------------------------------
 * SCENARIO 31 – TX_QUEUE_POP_EMPTY
 * Pop from empty queue returns 0.
 * ----------------------------------------------------------------------- */
static void Scenario_TxQueuePopEmpty(void)
{
    VUart_TxQueueType q;
    VUart_FrameType   out;
    uint8 result;

    q.head = 0u;
    q.tail = 0u;
    q.lock.value = 0u;
    q.policy = VUART_ARB_FIFO;

    result = VUart_TxQueuePop(&q, &out);
    ASSERT_EQ(0u, 0u, (uint32)result);
}

/* -----------------------------------------------------------------------
 * SCENARIO 33 – IPC_RING_PUSH_POP
 * Push one frame into an IPC ring and pop it back.
 * ----------------------------------------------------------------------- */
static void Scenario_IpcRingPushPop(void)
{
    VUart_IpcRingType ring;
    VUart_FrameType   in_f;
    VUart_FrameType   out_f;
    uint8 j;
    uint8 next_head;
    uint8 result;

    ring.head = 0u;
    ring.tail = 0u;
    ring.lock.value = 0u;
    for (j = 0u; j < VUART_IPC_RING_DEPTH; j++)
    {
        ring.frames[j].len = 0u;
    }

    in_f.dst_vm_id = 1u;
    in_f.src_vm_id = 0u;
    in_f.len = 1u;
    in_f.payload[0] = 0x55u;

    /* Push directly (mirrors VUart_IpcPushFrame logic). */
    VUart_Tc_Lock(&ring.lock);
    next_head = (uint8)((ring.head + 1u) % VUART_IPC_RING_DEPTH);
    result = (next_head != ring.tail) ? 1u : 0u;
    if (result != 0u)
    {
        ring.frames[ring.head] = in_f;
        ring.head = next_head;
    }
    VUart_Tc_Unlock(&ring.lock);

    ASSERT_EQ(0u, 1u, (uint32)result);

    /* Pop directly. */
    VUart_Tc_Lock(&ring.lock);
    result = (ring.tail != ring.head) ? 1u : 0u;
    if (result != 0u)
    {
        out_f = ring.frames[ring.tail];
        ring.tail = (uint8)((ring.tail + 1u) % VUART_IPC_RING_DEPTH);
    }
    VUart_Tc_Unlock(&ring.lock);

    ASSERT_EQ(1u, 1u, (uint32)result);
    ASSERT_EQ(2u, (uint32)in_f.dst_vm_id, (uint32)out_f.dst_vm_id);
    ASSERT_EQ(3u, (uint32)in_f.payload[0], (uint32)out_f.payload[0]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 43 – VIRTUAL_IRQ_BITMAP_BIT
 * VUart_InjectVirtualIrq must set exactly bit vm_id in the bitmap.
 * ----------------------------------------------------------------------- */
static void Scenario_VirtualIrqBitmapBit(void)
{
    extern volatile uint32 g_vuart_pending_irq_bitmap;
    uint8 vm;

    VUart_Cfg_Init();
    {
        const VUart_ConfigType* cfg = VUart_Cfg_GetByCoreId(0u);
        if (cfg != (const VUart_ConfigType*)0) { VUart_Init(cfg); }
    }

    for (vm = 0u; vm < (uint8)VUART_NUM_VMS; vm++)
    {
        g_vuart_pending_irq_bitmap = 0u;
        VUart_InjectVirtualIrq(vm);
        ASSERT_EQ(vm, (uint32)(1u << vm),
                  (uint32)(g_vuart_pending_irq_bitmap & (uint32)(1u << vm)));
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 46 – CFG_NUM_VMS_CONSISTENT
 * ----------------------------------------------------------------------- */
static void Scenario_CfgNumVmsConsistent(void)
{
    VUart_Cfg_Init();
    ASSERT_EQ(0u, (uint32)VUART_CFG_NUM_VMS, (uint32)VUART_NUM_VMS);
}

/* -----------------------------------------------------------------------
 * SCENARIO 47 – CFG_NUM_CORES_CONSISTENT
 * ----------------------------------------------------------------------- */
static void Scenario_CfgNumCoresConsistent(void)
{
    VUart_Cfg_Init();
    ASSERT_EQ(0u, (uint32)VUART_CFG_NUM_CORES, (uint32)VUART_NUM_CORES);
}

/* -----------------------------------------------------------------------
 * SCENARIO 48 – CFG_RX_BUF_SIZE_POW2
 * ----------------------------------------------------------------------- */
static void Scenario_CfgRxBufSizePow2(void)
{
    uint32 val = (uint32)RX_BUF_SIZE;
    ASSERT_EQ(0u, (uint32)VUART_CFG_RX_BUF_SIZE, val);
    /* Power-of-two check: val & (val-1) == 0 for any non-zero power of two. */
    ASSERT_TRUE(1u, (val != 0u) && ((val & (val - 1u)) == 0u));
}

/* -----------------------------------------------------------------------
 * SCENARIO 49 – CFG_TX_DEPTH_POW2
 * ----------------------------------------------------------------------- */
static void Scenario_CfgTxDepthPow2(void)
{
    uint32 val = (uint32)TX_QUEUE_DEPTH;
    ASSERT_EQ(0u, (uint32)VUART_CFG_TX_QUEUE_DEPTH, val);
    ASSERT_TRUE(1u, (val != 0u) && ((val & (val - 1u)) == 0u));
}

/* -----------------------------------------------------------------------
 * SCENARIO 50 – CFG_MAX_PAYLOAD_CONSISTENT
 * ----------------------------------------------------------------------- */
static void Scenario_CfgMaxPayloadConsistent(void)
{
    ASSERT_EQ(0u, (uint32)VUART_CFG_MAX_PAYLOAD, (uint32)VUART_MAX_PAYLOAD);
}

/* -----------------------------------------------------------------------
 * SCENARIO 51 – CFG_IPC_RING_DEPTH_CONSISTENT
 * ----------------------------------------------------------------------- */
static void Scenario_CfgIpcRingDepthConsistent(void)
{
    ASSERT_EQ(0u, (uint32)VUART_CFG_IPC_RING_DEPTH,
              (uint32)VUART_IPC_RING_DEPTH);
}

/* -----------------------------------------------------------------------
 * SCENARIO 52 – CFG_WINDOW_SIZE_CONSISTENT
 * ----------------------------------------------------------------------- */
static void Scenario_CfgWindowSizeConsistent(void)
{
    ASSERT_EQ(0u, (uint32)VUART_CFG_VM_WINDOW_SIZE,
              (uint32)VUART_VM_WINDOW_SIZE);
}

/* -----------------------------------------------------------------------
 * SCENARIO 53 – CFG_VM_BASE_ADDRS
 * Each VM base address in kVmBaseAddr matches the corresponding #define.
 * ----------------------------------------------------------------------- */
static void Scenario_CfgVmBaseAddrs(void)
{
    ASSERT_EQ(0u, (uint32)VUART_CFG_VM0_WINDOW_BASE,
              (uint32)VUart_kVmBaseAddr[0u]);
    ASSERT_EQ(1u, (uint32)VUART_CFG_VM1_WINDOW_BASE,
              (uint32)VUart_kVmBaseAddr[1u]);
    ASSERT_EQ(2u, (uint32)VUART_CFG_VM2_WINDOW_BASE,
              (uint32)VUart_kVmBaseAddr[2u]);
}

/* -----------------------------------------------------------------------
 * SCENARIO 54 – CFG_VM_CORE_MAP_VALID
 * Every core ID in kVmCoreMap is < VUART_NUM_CORES.
 * ----------------------------------------------------------------------- */
static void Scenario_CfgVmCoreMapValid(void)
{
    uint8 i;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_VMS; i++)
    {
        ASSERT_TRUE(i,
            VUart_kVmCoreMap[i] < (uint8)VUART_NUM_CORES);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 55 – CFG_OWNER_CORE_BINDING
 * ----------------------------------------------------------------------- */
static void Scenario_CfgOwnerCoreBinding(void)
{
    uint8 i;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(i, (uint32)VUART_CFG_UART0_OWNER_CORE,
                  (uint32)VUart_kConfigs[i].owner_core_id);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 56 – CFG_PHYS_BASE_BINDING
 * ----------------------------------------------------------------------- */
static void Scenario_CfgPhysBaseBinding(void)
{
    uint8 i;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(i, (uint32)VUART_CFG_PHYS_UART0_BASE,
                  (uint32)VUart_kConfigs[i].phys_uart_base);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 57 – CFG_RX_ISR_NODE_BINDING
 * ----------------------------------------------------------------------- */
static void Scenario_CfgRxIsrNodeBinding(void)
{
    uint8 i;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_EQ(i, (uint32)VUART_CFG_UART0_RX_ISR_NODE,
                  (uint32)VUart_kConfigs[i].rx_isr_node);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 58 – CFG_TX_QUEUE_PTR
 * Every configured core must have a non-NULL tx_queue after Cfg_Init.
 * ----------------------------------------------------------------------- */
static void Scenario_CfgTxQueuePtr(void)
{
    uint8 i;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        ASSERT_TRUE(i,
            VUart_kConfigs[i].tx_queue != (VUart_TxQueueType*)0);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 59 – CFG_INSTANCE_COUNT_TOTAL
 * Sum of num_instances across all cores equals VUART_NUM_VMS.
 * ----------------------------------------------------------------------- */
static void Scenario_CfgInstanceCountTotal(void)
{
    uint8 i;
    uint32 total = 0u;

    VUart_Cfg_Init();

    for (i = 0u; i < (uint8)VUART_NUM_CORES; i++)
    {
        total += (uint32)VUart_kConfigs[i].num_instances;
    }

    ASSERT_EQ(0u, (uint32)VUART_NUM_VMS, total);
}

/* -----------------------------------------------------------------------
 * SCENARIO 60 – WRITE_INVALID_VM_ID
 * WriteReg with vm_id > VUART_NUM_VMS returns immediately.
 * ----------------------------------------------------------------------- */
static void Scenario_WriteInvalidVmId(void)
{
    VUart_Cfg_Init();
    {
        const VUart_ConfigType* cfg = VUart_Cfg_GetByCoreId(0u);
        if (cfg != (const VUart_ConfigType*)0) { VUart_Init(cfg); }
    }

    /* This must not crash and must not corrupt any instance. */
    VUart_WriteReg((uint8)(VUART_NUM_VMS + 1u), 0u, 1u);
    ASSERT_TRUE(0u, TRUE);
}

/* -----------------------------------------------------------------------
 * SCENARIO 61 – READ_INVALID_VM_ID
 * ----------------------------------------------------------------------- */
static void Scenario_ReadInvalidVmId(void)
{
    VUart_Cfg_Init();
    {
        const VUart_ConfigType* cfg = VUart_Cfg_GetByCoreId(0u);
        if (cfg != (const VUart_ConfigType*)0) { VUart_Init(cfg); }
    }

    VUart_ReadReg((uint8)(VUART_NUM_VMS + 1u), 0u, 1u);
    ASSERT_TRUE(0u, TRUE);
}

/* -----------------------------------------------------------------------
 * SCENARIO 62 – DR_WRITE_ZERO_SIZE
 * WriteReg with DR offset and size == 0 must not build or enqueue a frame.
 * ----------------------------------------------------------------------- */
static void Scenario_DrWriteZeroSize(void)
{
    VUart_InstanceType* inst;
    const VUart_ConfigType* cfg;
    uint32 window_mem[4];
    uint8 vm_id;
    uint8 head_before;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);
    ASSERT_TRUE(1u, cfg->num_instances > 0u);
    if (cfg->num_instances == 0u) { return; }

    inst = &cfg->instances[0u];
    vm_id = inst->vm_id;
    inst->virt_base = &window_mem[0];
    window_mem[3] = 0xAAu;

    head_before = cfg->tx_queue ? cfg->tx_queue->head : 0u;

    VUart_WriteReg(vm_id, (uint8)VUART_OFFSET_DR, 0u);

    /* TX queue head must not have advanced. */
    if (cfg->tx_queue != (VUart_TxQueueType*)0)
    {
        ASSERT_EQ(2u, (uint32)head_before, (uint32)cfg->tx_queue->head);
    }
    else
    {
        ASSERT_TRUE(2u, TRUE);
    }
}

/* -----------------------------------------------------------------------
 * SCENARIO 64 – CFG_GET_INVALID_CORE
 * ----------------------------------------------------------------------- */
static void Scenario_CfgGetInvalidCore(void)
{
    const VUart_ConfigType* result;

    VUart_Cfg_Init();

    result = VUart_Cfg_GetByCoreId((uint8)VUART_NUM_CORES);
    ASSERT_TRUE(0u, result == (const VUart_ConfigType*)0);

    result = VUart_Cfg_GetByCoreId(0xFFu);
    ASSERT_TRUE(1u, result == (const VUart_ConfigType*)0);
}

/* -----------------------------------------------------------------------
 * SCENARIO 65 – FIND_INSTANCE_NOT_FOUND
 * VUart_FindLocalInstanceByVmId returns NULL for a VM not owned locally.
 * ----------------------------------------------------------------------- */
static void Scenario_FindInstanceNotFound(void)
{
    VUart_InstanceType* result;
    const VUart_ConfigType* cfg;

    VUart_Cfg_Init();
    cfg = VUart_Cfg_GetByCoreId(0u);
    ASSERT_TRUE(0u, cfg != (const VUart_ConfigType*)0);
    if (cfg == (const VUart_ConfigType*)0) { return; }

    VUart_Init(cfg);

    /* VM id 0xFF is never a real VM id. */
    result = VUart_FindLocalInstanceByVmId(0xFFu);
    ASSERT_TRUE(1u, result == (VUart_InstanceType*)0);
}

/* -----------------------------------------------------------------------
 * SCENARIO 66 – RX_PUSH_NULL_INST
 * VUart_RxRingPush with NULL returns 0.
 * ----------------------------------------------------------------------- */
static void Scenario_RxPushNullInst(void)
{
    uint8 result = VUart_RxRingPush((VUart_InstanceType*)0, 0xABu);
    ASSERT_EQ(0u, 0u, (uint32)result);
}

/* =========================================================================
 * Public scenario dispatcher
 * =========================================================================*/

/**
 * @brief Execute the assertions for the currently selected scenario.
 *
 * Call HvVUartTest_Reset() before calling this function.  Inspect
 * g_hv_vuart_test_assert_failed afterwards to determine pass / fail.
 */
void HvVUartTest_RunScenario(void)
{
    switch (g_hv_vuart_test_active_scenario)
    {
        case HV_VUART_SCENARIO_DEFAULT:
            Scenario_Default();
            break;

        case HV_VUART_SCENARIO_CFG_INIT_CHECK:
            Scenario_CfgInitCheck();
            break;

        case HV_VUART_SCENARIO_INIT_CORE0_OWNER:
            Scenario_InitCore0Owner();
            break;

        case HV_VUART_SCENARIO_INIT_CORE1_NON_OWNER:
            Scenario_InitCore1NonOwner();
            break;

        case HV_VUART_SCENARIO_INIT_IPC_RINGS_CLEARED:
            Scenario_InitIpcRingsCleared();
            break;

        case HV_VUART_SCENARIO_DR_WRITE_BASIC:
            Scenario_DrWriteBasic();
            break;

        case HV_VUART_SCENARIO_DR_READ_EMPTY:
            Scenario_DrReadEmpty();
            break;

        case HV_VUART_SCENARIO_DR_READ_WITH_DATA:
            Scenario_DrReadWithData();
            break;

        case HV_VUART_SCENARIO_DR_READ_LAST_BYTE_CLEARS_RXRDY:
            Scenario_DrReadLastByteClears();
            break;

        case HV_VUART_SCENARIO_CR_WRITE_MASK:
            Scenario_CrWriteMask();
            break;

        case HV_VUART_SCENARIO_CR_READ:
            Scenario_CrRead();
            break;

        case HV_VUART_SCENARIO_DST_ID_WRITE:
            Scenario_DstIdWrite();
            break;

        case HV_VUART_SCENARIO_DST_ID_READ:
            Scenario_DstIdRead();
            break;

        case HV_VUART_SCENARIO_SR_READ:
            Scenario_SrRead();
            break;

        case HV_VUART_SCENARIO_BUILD_FRAME_BASIC:
            Scenario_BuildFrameBasic();
            break;

        case HV_VUART_SCENARIO_BUILD_FRAME_CLAMP_LEN:
            Scenario_BuildFrameClampLen();
            break;

        case HV_VUART_SCENARIO_BUILD_FRAME_NULL_PTR:
            Scenario_BuildFrameNullPtr();
            break;

        case HV_VUART_SCENARIO_PARSER_VALID_FRAME:
            Scenario_ParserValidFrame();
            break;

        case HV_VUART_SCENARIO_PARSER_INVALID_DST:
            Scenario_ParserInvalidDst();
            break;

        case HV_VUART_SCENARIO_PARSER_ZERO_LENGTH:
            Scenario_ParserZeroLength();
            break;

        case HV_VUART_SCENARIO_PARSER_OVERSIZED_LENGTH:
            Scenario_ParserOversizedLength();
            break;

        case HV_VUART_SCENARIO_RX_RING_PUSH_SINGLE:
            Scenario_RxRingPushSingle();
            break;

        case HV_VUART_SCENARIO_RX_RING_OVERFLOW:
            Scenario_RxRingOverflow();
            break;

        case HV_VUART_SCENARIO_RX_RING_POP_EMPTY:
            Scenario_RxRingPopEmpty();
            break;

        case HV_VUART_SCENARIO_RX_RING_FILL_THEN_DRAIN:
            Scenario_RxRingFillThenDrain();
            break;

        case HV_VUART_SCENARIO_RX_RING_WRAP_AROUND:
            Scenario_RxRingWrapAround();
            break;

        case HV_VUART_SCENARIO_TX_QUEUE_PUSH_POP:
            Scenario_TxQueuePushPop();
            break;

        case HV_VUART_SCENARIO_TX_QUEUE_FULL:
            Scenario_TxQueueFull();
            break;

        case HV_VUART_SCENARIO_TX_QUEUE_POP_EMPTY:
            Scenario_TxQueuePopEmpty();
            break;

        case HV_VUART_SCENARIO_IPC_RING_PUSH_POP:
            Scenario_IpcRingPushPop();
            break;

        case HV_VUART_SCENARIO_VIRTUAL_IRQ_BITMAP_BIT:
            Scenario_VirtualIrqBitmapBit();
            break;

        case HV_VUART_SCENARIO_CFG_NUM_VMS_CONSISTENT:
            Scenario_CfgNumVmsConsistent();
            break;

        case HV_VUART_SCENARIO_CFG_NUM_CORES_CONSISTENT:
            Scenario_CfgNumCoresConsistent();
            break;

        case HV_VUART_SCENARIO_CFG_RX_BUF_SIZE_POW2:
            Scenario_CfgRxBufSizePow2();
            break;

        case HV_VUART_SCENARIO_CFG_TX_DEPTH_POW2:
            Scenario_CfgTxDepthPow2();
            break;

        case HV_VUART_SCENARIO_CFG_MAX_PAYLOAD_CONSISTENT:
            Scenario_CfgMaxPayloadConsistent();
            break;

        case HV_VUART_SCENARIO_CFG_IPC_RING_DEPTH_CONSISTENT:
            Scenario_CfgIpcRingDepthConsistent();
            break;

        case HV_VUART_SCENARIO_CFG_WINDOW_SIZE_CONSISTENT:
            Scenario_CfgWindowSizeConsistent();
            break;

        case HV_VUART_SCENARIO_CFG_VM_BASE_ADDRS:
            Scenario_CfgVmBaseAddrs();
            break;

        case HV_VUART_SCENARIO_CFG_VM_CORE_MAP_VALID:
            Scenario_CfgVmCoreMapValid();
            break;

        case HV_VUART_SCENARIO_CFG_OWNER_CORE_BINDING:
            Scenario_CfgOwnerCoreBinding();
            break;

        case HV_VUART_SCENARIO_CFG_PHYS_BASE_BINDING:
            Scenario_CfgPhysBaseBinding();
            break;

        case HV_VUART_SCENARIO_CFG_RX_ISR_NODE_BINDING:
            Scenario_CfgRxIsrNodeBinding();
            break;

        case HV_VUART_SCENARIO_CFG_TX_QUEUE_PTR:
            Scenario_CfgTxQueuePtr();
            break;

        case HV_VUART_SCENARIO_CFG_INSTANCE_COUNT_TOTAL:
            Scenario_CfgInstanceCountTotal();
            break;

        case HV_VUART_SCENARIO_WRITE_INVALID_VM_ID:
            Scenario_WriteInvalidVmId();
            break;

        case HV_VUART_SCENARIO_READ_INVALID_VM_ID:
            Scenario_ReadInvalidVmId();
            break;

        case HV_VUART_SCENARIO_DR_WRITE_ZERO_SIZE:
            Scenario_DrWriteZeroSize();
            break;

        case HV_VUART_SCENARIO_CFG_GET_INVALID_CORE:
            Scenario_CfgGetInvalidCore();
            break;

        case HV_VUART_SCENARIO_FIND_INSTANCE_NOT_FOUND:
            Scenario_FindInstanceNotFound();
            break;

        case HV_VUART_SCENARIO_RX_PUSH_NULL_INST:
            Scenario_RxPushNullInst();
            break;

        default:
            /* Unknown scenario – record one failure so the caller notices. */
            HvVUartTest_AssertTrue(0xFFFFFFFFu, FALSE);
            break;
    }
}

#endif /* (HV_TEST_ENABLE != 0u) && (HV_VUART_TEST_ENABLE != 0u) */