/*
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for STM32H7RS CM7 processor
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/cache.h>
#include <soc.h>
#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>
#include <stm32_ll_rcc.h>

#include <cmsis_core.h>

#define PWR_NODE DT_INST(0, st_stm32h7rs_pwr)

/* Helper to simplify following #if chain */
#define SELECTED_PSU(_x) DT_ENUM_HAS_VALUE(PWR_NODE, power_supply, _x)

/*
 * Bound for the PWR status-flag polls below. The core runs at 64 MHz (HSI) at
 * this point, so this is a few milliseconds -- far longer
 * than the regulator needs, short enough not to look like a hang.
 */
#define STM32H7RS_PWR_FLAG_TIMEOUT 20000U

#if SELECTED_PSU(ldo)
#define SELECTED_POWER_SUPPLY LL_PWR_LDO_SUPPLY
#elif SELECTED_PSU(external_source)
#define SELECTED_POWER_SUPPLY LL_PWR_EXTERNAL_SOURCE_SUPPLY
#elif SELECTED_PSU(smps_direct)
#define SELECTED_POWER_SUPPLY LL_PWR_DIRECT_SMPS_SUPPLY
#elif SELECTED_PSU(smps_ext_ldo)
#define SELECTED_POWER_SUPPLY LL_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO
#elif SELECTED_PSU(smps_ext_bypass)
#define SELECTED_POWER_SUPPLY LL_PWR_SMPS_1V8_SUPPLIES_EXT
#endif

/**
 * @brief Perform basic hardware initialization at boot.
 *
 * This needs to be run from the very beginning.
 */
void soc_early_init_hook(void)
{
	sys_cache_instr_enable();
	sys_cache_data_enable();

	/* Update CMSIS SystemCoreClock variable (HCLK) */
	/* At reset, system core clock is set to 64 MHz from HSI */
	SystemCoreClock = 64000000;

	/* Power Configuration */
	LL_PWR_ConfigSupply(SELECTED_POWER_SUPPLY);

	/*
	 * RM0477 section 6.5: after the supply configuration is programmed the
	 * system is in Run* mode, and "VOS can be changed only when ACTVOSRDY
	 * is valid". Waiting for ACTVOSRDY here is mandatory; without it the
	 * VOS write can be ignored and every later VOSRDY poll spins forever.
	 *
	 * Both waits are bounded: if the supply configuration does not match
	 * the board, VCORE is clamped to 1.0 V by the overvoltage protection
	 * and these flags never assert. Timing out leaves the board booting
	 * (and debuggable) instead of deadlocked before the console exists.
	 */
	for (uint32_t i = 0; i < STM32H7RS_PWR_FLAG_TIMEOUT; i++) {
		if (LL_PWR_IsActiveFlag_ACTVOSRDY() != 0) {
			break;
		}
	}

	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);

	for (uint32_t i = 0; i < STM32H7RS_PWR_FLAG_TIMEOUT; i++) {
		if (LL_PWR_IsActiveFlag_VOSRDY() != 0) {
			break;
		}
	}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpioo), okay) || DT_NODE_HAS_STATUS(DT_NODELABEL(gpiop), okay)
	LL_PWR_EnableXSPIM1(); /* Required for powering GPIO O and P */
#endif /* gpioo || gpio p */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpion), okay)
	LL_PWR_EnableXSPIM2(); /* Required for powering GPIO N */
#endif /* gpio n */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpiom), okay)
	LL_PWR_EnableUSBVoltageDetector(); /* Required for powering GPIO M */
#endif /* gpiom */
}
