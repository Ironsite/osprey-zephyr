/*
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for STM32N6 processor
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>

#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_icache.h>

#include <cmsis_core.h>

#define LOG_LEVEL CONFIG_SOC_LOG_LEVEL
LOG_MODULE_REGISTER(soc);

extern char _vector_start[];
void *g_pfnVectors = (void *)_vector_start;

#if defined(CONFIG_SOC_RESET_HOOK)
void soc_reset_hook(void)
{
	/* This is provided by STM32Cube HAL */
	SystemInit();

	/* SystemInit() in the FSBL variant disables GPIOG clock.
	 * Re-enable all GPIO clocks via AHB4ENSR (set register).
	 */
	*(volatile uint32_t *)(0x56028000 + 0x0A5C) = 0x0001E0FF;

#if !defined(CONFIG_XIP) || !defined(CONFIG_STM32_APP_IN_EXT_FLASH)
	/* Only reconfigure XSPI2 clocks/routing if NOT executing from external flash.
	 * If running XIP from XSPI2 (memory-mapped), touching these registers
	 * will kill the bus we're executing from → hard lockup.
	 */

	/* Reset XSPI2 and the XSPI I/O manager before touching anything else.
	 *
	 * Whatever ran before us can leave the block mid-transaction: the ROM's
	 * memory-mapped read of this FSBL, or an external loader from a previous
	 * SWD session. In that state CR.BUSY never clears, so every command --
	 * even a reset opcode with no address and no data phase -- times out, and
	 * HAL_XSPI_Abort() times out too. MEASURED: only a power cycle recovered
	 * it; NRST and the debugger's -hardRst did not.
	 *
	 * Nothing else resets it. The Zephyr XSPI driver has no reset-line
	 * handling and the xspi2 DT node declares no `resets` property, so the
	 * driver just re-configures a stuck peripheral. Pulse it here instead.
	 *
	 * RCC AHB5RSTSR (0x0A20) asserts, AHB5RSTCR (0x1220) releases.
	 * XSPI2 = bit 12, XSPIM = bit 13.
	 */
	*(volatile uint32_t *)(0x56028000 + 0x0A60) = (1 << 12) | (1 << 13);
	*(volatile uint32_t *)(0x56028000 + 0x0A20) = (1 << 12) | (1 << 13);
	for (volatile int i = 0; i < 1000; i++) {
		__asm volatile("nop");
	}
	*(volatile uint32_t *)(0x56028000 + 0x1220) = (1 << 12) | (1 << 13);

	/* Set XSPI2 kernel clock source to HCLK (200 MHz from PLL1).
	 * RCC CCIPR6 offset = 0x0158, XSPI2SEL bits [5:4] = 0b00 for HCLK
	 */
	volatile uint32_t *ccipr6 = (volatile uint32_t *)(0x56028000 + 0x0158);
	*ccipr6 = (*ccipr6 & ~(3u << 4)) | (0 << 4);

	/* Enable XSPIM and XSPI2 clocks (AHB5ENSR offset 0x0A60)
	 * XSPI1=bit5, XSPI2=bit12, XSPIM=bit13
	 */
	*(volatile uint32_t *)(0x56028000 + 0x0A60) = (1 << 5) | (1 << 12) | (1 << 13);

	/* Disable XSPI1 and XSPI2 clocks for XSPIM config */
	*(volatile uint32_t *)(0x56028000 + 0x1260) = (1 << 5) | (1 << 12); /* ENCR = clear */

	/* Write XSPIM CR: nCSOverride=disabled(0), Req2AckTime=1 */
	*(volatile uint32_t *)0x5802B400 = (1 << 4); /* REQ2ACK_TIME = 1 */

	/* Re-enable XSPI1 and XSPI2 clocks */
	*(volatile uint32_t *)(0x56028000 + 0x0A60) = (1 << 5) | (1 << 12);

	/* Wait for flash power-up after NRST reset */
	for (volatile int i = 0; i < 40000; i++) {
		__asm volatile("nop");
	}
#endif /* !CONFIG_XIP || !CONFIG_STM32_APP_IN_EXT_FLASH */
}
#endif

#define RIF_MASTER_CID1_SEC_PRIV(device)	\
	do {										\
		RIMC_MasterConfig_t rimc = {						\
			.MasterCID = RIF_CID_1,						\
			.SecPriv = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV,		\
		};									\
		HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_##device, &rimc);	\
	} while (0)

#define RIF_SLAVE_SEC_PRIV(device)	\
	HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_##device,		\
					      RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV)

static void soc_rif_config(void)
{
#if defined(CONFIG_TRUSTED_EXECUTION_SECURE)
	/* Enable the clock for the RIFSC (RIF Security Controller) */
	__HAL_RCC_RIFSC_CLK_ENABLE();

	/* ADC */
	RIF_SLAVE_SEC_PRIV(ADC12);
	/* DCMIPP */
	RIF_MASTER_CID1_SEC_PRIV(DCMIPP);
	RIF_SLAVE_SEC_PRIV(DCMIPP);
	/* DMA2D */
	RIF_MASTER_CID1_SEC_PRIV(DMA2D);
	RIF_SLAVE_SEC_PRIV(DMA2D);
	/* ETH */
	RIF_MASTER_CID1_SEC_PRIV(ETH1);
	RIF_SLAVE_SEC_PRIV(ETH1);
	/* JPEG */
	RIF_SLAVE_SEC_PRIV(JPEG);
	/* LTDC Layer 1 */
	RIF_MASTER_CID1_SEC_PRIV(LTDC1);
	RIF_SLAVE_SEC_PRIV(LTDCL1);
#ifdef NPU_PRESENT
	/* NPU */
	RIF_MASTER_CID1_SEC_PRIV(NPU);
	RIF_SLAVE_SEC_PRIV(NPU);
#endif
	/* VENC */
	RIF_MASTER_CID1_SEC_PRIV(VENC);
	RIF_SLAVE_SEC_PRIV(VENC);
#endif /* CONFIG_TRUSTED_EXECUTION_SECURE */
}

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * This needs to be run from the very beginning.
 *
 * @return 0
 */
void soc_early_init_hook(void)
{
	/* Enable caches */
	sys_cache_instr_enable();
	sys_cache_data_enable();

	/* Update CMSIS SystemCoreClock variable (HCLK) */
	/* At reset, system core clock is set to 64 MHz from HSI */
	SystemCoreClock = 64000000;

	/* Enable PWR */
	LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_PWR);

	/* Set the main internal Regulator output voltage for best performance */
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE0);

	/* Enable IOs */
	LL_PWR_EnableVddIO2();
	LL_PWR_EnableVddIO3();
	LL_PWR_EnableVddIO4();
	LL_PWR_EnableVddIO5();

	/* RIF configuration */
	if (IS_ENABLED(CONFIG_STM32N6_RIF_OPEN)) {
		soc_rif_config();
	}
}
