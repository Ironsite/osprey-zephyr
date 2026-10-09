/*
 * Copyright (c) 2020 Mario Jaun
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(mac))
#define sram_eth_node DT_PHANDLE(DT_NODELABEL(mac), memory_regions)

#if DT_NODE_HAS_STATUS_OKAY(sram_eth_node)
/*
 * The descriptor slot and the region size used to be hard-coded here as
 * "descriptors at +0, buffers at +256, section ends at +16K". Both numbers
 * only held for the HAL's default ETH_RX_DESC_CNT of 4:
 *
 *   - 4 RX + 4 TX descriptors is 8 * 24 = 192 bytes, which fits under 256.
 *     Raising ETH_RX_DESC_CNT to 16 makes it 20 * 24 = 480, and the
 *     "buffers at +256" line then tries to move the location counter
 *     BACKWARDS over the descriptors, which ld refuses to do:
 *         cannot move location counter backwards (from 300001e0 to 30000100)
 *   - the 16K tail padding forced the section to occupy 16K no matter what
 *     was in it, so a region larger than 16K could not be used and a region
 *     smaller than 16K overflowed silently in whichever direction.
 *
 * Laying the two arrays out back-to-back instead makes both follow whatever
 * ETH_RX_DESC_CNT is set to, and lets the linker do the bounds check: if the
 * descriptors and buffers do not fit the DT region, the link fails with a
 * region-overflow error naming the shortfall, rather than producing an image
 * whose DMA quietly writes past the end.
 *
 * ALIGN(32) before the buffers keeps them on a cache line, which is what the
 * driver's __aligned(32) already asks for and what the D-cache maintenance in
 * the non-nocache configurations depends on.
 */
SECTION_DATA_PROLOGUE(eth_stm32,(NOLOAD),)
{
    . = ABSOLUTE(DT_REG_ADDR(sram_eth_node));
    *(.eth_stm32_desc)
    . = ALIGN(32);
    *(.eth_stm32_buf)
} GROUP_DATA_LINK_IN(LINKER_DT_NODE_REGION_NAME(sram_eth_node), LINKER_DT_NODE_REGION_NAME(sram_eth_node))
#endif /* DT_NODE_HAS_STATUS_OKAY(sram_eth_node) */

#endif /* DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(mac)) */
