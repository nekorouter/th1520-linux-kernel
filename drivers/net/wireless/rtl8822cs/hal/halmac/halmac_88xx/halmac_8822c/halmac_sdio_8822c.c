/******************************************************************************
 *
 * Copyright(c) 2017 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_8822c_cfg.h"
#include "halmac_sdio_8822c.h"
#include "halmac_pwr_seq_8822c.h"
#include "../halmac_init_88xx.h"
#include "../halmac_common_88xx.h"
#include "../halmac_sdio_88xx.h"

#if (HALMAC_8822C_SUPPORT && HALMAC_SDIO_SUPPORT)

#define WLAN_ACQ_NUM_MAX	8

static enum halmac_ret_status
chk_oqt_8822c(struct halmac_adapter *adapter, u32 tx_agg_num, u8 *buf,
	      u8 macid_cnt);

static enum halmac_ret_status
update_oqt_free_space_8822c(struct halmac_adapter *adapter);

static enum halmac_ret_status
update_sdio_free_page_8822c(struct halmac_adapter *adapter);

static enum halmac_ret_status
update_ac_empty_8822c(struct halmac_adapter *adapter, u8 value);

static enum halmac_ret_status
chk_qsel_8822c(struct halmac_adapter *adapter, u8 qsel_first, u8 *pkt,
	       u8 *macid_cnt);

static enum halmac_ret_status
chk_dma_mapping_8822c(struct halmac_adapter *adapter, u16 **cur_fs,
		      u8 qsel_first);

static enum halmac_ret_status
chk_rqd_page_num_8822c(struct halmac_adapter *adapter, u8 *buf, u32 *rqd_pg_num,
		       u16 **cur_fs, u8 *macid_cnt, u32 tx_agg_num);

/**
 * init_sdio_cfg_8822c() - init SDIO
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_sdio_cfg_8822c(struct halmac_adapter *adapter)
{
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (adapter->intf != HALMAC_INTERFACE_SDIO)
		return HALMAC_RET_WRONG_INTF;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_R32(REG_SDIO_FREE_TXPG);

	value32 = HALMAC_REG_R32(REG_SDIO_TX_CTRL) & 0xFFFF;
	value32 &= ~(BIT_CMD_ERR_STOP_INT_EN | BIT_EN_MASK_TIMER |
			BIT_EN_RXDMA_MASK_INT | BIT_CMD53_TX_FORMAT);
	HALMAC_REG_W32(REG_SDIO_TX_CTRL, value32);

	HALMAC_REG_W8_SET(REG_SDIO_BUS_CTRL, BIT_EN_RPT_TXCRC);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * mac_pwr_switch_sdio_8822c() - switch mac power
 * @adapter : the adapter of halmac
 * @pwr : power state
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
mac_pwr_switch_sdio_8822c(struct halmac_adapter *adapter,
			  enum halmac_mac_power pwr)
{
	u8 value8;
	u8 rpwm;
	u32 imr_backup;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);
	PLTFM_MSG_TRACE("[TRACE]8822C pwr seq ver = %s\n",
			HALMAC_8822C_PWR_SEQ_VER);

	adapter->rpwm = HALMAC_REG_R8(REG_SDIO_HRPWM1);

	/* Check FW still exist or not */
	if (HALMAC_REG_R16(REG_MCUFW_CTRL) == 0xC078) {
		/* Leave 32K */
		rpwm = (u8)((adapter->rpwm ^ BIT(7)) & 0x80);
		HALMAC_REG_W8(REG_SDIO_HRPWM1, rpwm);
	}

	value8 = HALMAC_REG_R8(REG_CR);
	if (value8 == 0xEA)
		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_OFF;
	else
		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_ON;

	/*Check if power switch is needed*/
	if (pwr == HALMAC_MAC_POWER_ON &&
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_ON) {
		PLTFM_MSG_WARN("[WARN]power state unchange!!\n");
		return HALMAC_RET_PWR_UNCHANGE;
	}

	imr_backup = HALMAC_REG_R32(REG_SDIO_HIMR);
	HALMAC_REG_W32(REG_SDIO_HIMR, 0);

	if (pwr == HALMAC_MAC_POWER_OFF) {
		adapter->pwr_off_flow_flag = 1;
		if (pwr_seq_parser_88xx(adapter, card_dis_flow_8822c) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]Handle power off cmd error\n");
			HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);
			return HALMAC_RET_POWER_OFF_FAIL;
		}

		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_OFF;
		adapter->halmac_state.dlfw_state = HALMAC_DLFW_NONE;
		adapter->pwr_off_flow_flag = 0;
		init_adapter_dynamic_param_88xx(adapter);
	} else {
		if (pwr_seq_parser_88xx(adapter, card_en_flow_8822c) !=
		    HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]Handle power on cmd error\n");
			HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);
			return HALMAC_RET_POWER_ON_FAIL;
		}
		adapter->sdio_hw_info.tx_seq = 1;
		adapter->halmac_state.mac_pwr = HALMAC_MAC_POWER_ON;
	}

	HALMAC_REG_W32(REG_SDIO_HIMR, imr_backup);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_tx_allowed_sdio_88xx() - check tx status
 * @adapter : the adapter of halmac
 * @buf : tx packet, include txdesc
 * @size : tx packet size, include txdesc
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
tx_allowed_sdio_8822c(struct halmac_adapter *adapter, u8 *buf, u32 size)
{
	u16 *cur_fs = NULL;
	u32 cnt;
	u32 tx_agg_num;
	u32 rqd_pg_num = 0;
	u8 macid_cnt = 0;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_qsel qsel;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	tx_agg_num = GET_TX_DESC_DMA_TXAGG_NUM(buf);
	tx_agg_num = (tx_agg_num == 0) ? 1 : tx_agg_num;

	status = chk_rqd_page_num_8822c(adapter, buf, &rqd_pg_num, &cur_fs,
					&macid_cnt, tx_agg_num);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	qsel = (enum halmac_qsel)GET_TX_DESC_QSEL(buf);
	if (qsel == HALMAC_QSEL_BCN || qsel == HALMAC_QSEL_CMD)
		return HALMAC_RET_SUCCESS;

	cnt = 10;
	do {
		if ((u32)(*cur_fs + fs_info->pubq_pg_num) > rqd_pg_num) {
			status = chk_oqt_8822c(adapter, tx_agg_num, buf,
					       macid_cnt);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_WARN("[WARN]oqt buffer full, cnt = %d\n", cnt);
				return status;
			}

			if (*cur_fs >= rqd_pg_num) {
				*cur_fs -= (u16)rqd_pg_num;
			} else {
				fs_info->pubq_pg_num -=
						(u16)(rqd_pg_num - *cur_fs);
				*cur_fs = 0;
			}

			break;
		}

		update_sdio_free_page_8822c(adapter);

		cnt--;
		if (cnt == 0)
			return HALMAC_RET_FREE_SPACE_NOT_ENOUGH;
	} while (1);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * cfg_tx_fmt_sdio_8822c() - config sdio tx address format
 * @adapter : the adapter of halmac
 * @fmt : tx address format
 * Author : Soar
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
cfg_tx_fmt_sdio_8822c(struct halmac_adapter *adapter,
		      enum halmac_sdio_tx_format fmt)
{
	u8 value;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	value = HALMAC_REG_R8(REG_SDIO_TX_CTRL + 1);

	if (fmt == HALMAC_SDIO_AGG_MODE) {
		HALMAC_REG_W8(REG_SDIO_TX_CTRL + 1, value & ~(BIT(5)));
	} else if (fmt == HALMAC_SDIO_DUMMY_BLOCK_MODE ||
		   fmt == HALMAC_SDIO_DUMMY_AUTO_MODE) {
		if ((value & BIT(6)) == 0) {
			HALMAC_REG_W8(REG_SDIO_TX_CTRL + 1, value | BIT(5));
		} else {
			if ((value & BIT(5)) == 0)
				return HALMAC_RET_SDIO_MIX_MODE;
			else
				return HALMAC_RET_SDIO_SEQ_FAIL;
		}
	} else {
		PLTFM_MSG_ERR("sdio tx address format = %d\n", fmt);
		return HALMAC_RET_NOT_SUPPORT;
	}

	adapter->sdio_hw_info.tx_addr_format = fmt;

	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_8_sdio_88xx() - read 1byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u8
reg_r8_sdio_8822c(struct halmac_adapter *adapter, u32 offset)
{
	u8 value8;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;

	if ((offset & 0xFFFF0000) == 0) {
		if (adapter->pwr_off_flow_flag == 1 ||
		    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
		    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R) {
			value8 = (u8)r_indir_sdio_88xx(adapter, offset,
						       HALMAC_IO_BYTE);
		} else {
			offset |= WLAN_IOREG_OFFSET;
			status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]convert offset\n");
				return status;
			}
			value8 = (u8)PLTFM_SDIO_CMD53_R8(offset);
		}
	} else {
		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}
		value8 = PLTFM_SDIO_CMD52_R(offset);
	}

	return value8;
}

/**
 * halmac_reg_write_8_sdio_88xx() - write 1byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w8_sdio_8822c(struct halmac_adapter *adapter, u32 offset, u8 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;

	if ((adapter->pwr_off_flow_flag == 1 ||
	     adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
	     cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	     cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R) &&
	    (offset & 0xFFFF0000) == 0) {
		w_indir_sdio_88xx(adapter, offset, value, HALMAC_IO_BYTE);
	} else {
		if ((offset & 0xFFFF0000) == 0)
			offset |= WLAN_IOREG_OFFSET;
		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}
		PLTFM_SDIO_CMD52_W(offset, value);
	}
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_reg_read_16_sdio_88xx() - read 2byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u16
reg_r16_sdio_8822c(struct halmac_adapter *adapter, u32 offset)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	union {
		__le16 word;
		u8 byte[2];
	} value16 = { 0x0000 };

	if ((offset & 0xFFFF0000) == 0 &&
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		return (u16)r_indir_sdio_88xx(adapter, offset, HALMAC_IO_WORD);
	} else if ((offset & 0xFFFF0000) != 0 &&
		   adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		value16.byte[0] = PLTFM_SDIO_CMD52_R(offset);
		value16.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);
		return rtk_le16_to_cpu(value16.word);
	}

	if ((offset & 0xFFFF0000) == 0)
		offset |= WLAN_IOREG_OFFSET;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]convert offset\n");
		return status;
	}

	if (((offset & (2 - 1)) != 0) ||
	    adapter->sdio_cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    adapter->sdio_cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R) {
		value16.byte[0] = (u8)PLTFM_SDIO_CMD53_R32(offset);
		value16.byte[1] = (u8)PLTFM_SDIO_CMD53_R32(offset + 1);
		return rtk_le16_to_cpu(value16.word);
	}

	return PLTFM_SDIO_CMD53_R16(offset);
}

/**
 * halmac_reg_write_16_sdio_88xx() - write 2byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w16_sdio_8822c(struct halmac_adapter *adapter, u32 offset, u16 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
	    ((offset & (2 - 1)) != 0) ||
	    adapter->sdio_cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    adapter->sdio_cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_W) {
		if ((offset & 0xFFFF0000) == 0 && ((offset & (2 - 1)) == 0)) {
			status = w_indir_sdio_88xx(adapter, offset, value,
						   HALMAC_IO_WORD);
		} else {
			if ((offset & 0xFFFF0000) == 0)
				offset |= WLAN_IOREG_OFFSET;

			status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]convert offset\n");
				return status;
			}
			PLTFM_SDIO_CMD52_W(offset, (u8)(value & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((value & 0xFF00) >> 8));
		}
	} else {
		if ((offset & 0xFFFF0000) == 0)
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}

		PLTFM_SDIO_CMD53_W16(offset, value);
	}
	return status;
}

/**
 * halmac_reg_read_32_sdio_88xx() - read 4byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
u32
reg_r32_sdio_8822c(struct halmac_adapter *adapter, u32 offset)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	union {
		__le32 dword;
		u8 byte[4];
	} value32 = { 0x00000000 };

	if (((offset & 0xFFFF0000) == 0) &&
	    adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		return r_indir_sdio_88xx(adapter, offset, HALMAC_IO_DWORD);
	} else if (((offset & 0xFFFF0000) != 0) &&
		   adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF) {
		value32.byte[0] = PLTFM_SDIO_CMD52_R(offset);
		value32.byte[1] = PLTFM_SDIO_CMD52_R(offset + 1);
		value32.byte[2] = PLTFM_SDIO_CMD52_R(offset + 2);
		value32.byte[3] = PLTFM_SDIO_CMD52_R(offset + 3);
		return rtk_le32_to_cpu(value32.dword);
	}

	if (0 == (offset & 0xFFFF0000))
		offset |= WLAN_IOREG_OFFSET;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]convert offset\n");
		return status;
	}

	if ((offset & (4 - 1)) != 0) {
		value32.byte[0] = (u8)PLTFM_SDIO_CMD53_R32(offset);
		value32.byte[1] = (u8)PLTFM_SDIO_CMD53_R32(offset + 1);
		value32.byte[2] = (u8)PLTFM_SDIO_CMD53_R32(offset + 2);
		value32.byte[3] = (u8)PLTFM_SDIO_CMD53_R32(offset + 3);
		return rtk_le32_to_cpu(value32.dword);
	}

	return PLTFM_SDIO_CMD53_R32(offset);
}

/**
 * halmac_reg_write_32_sdio_88xx() - write 4byte register
 * @adapter : the adapter of halmac
 * @offset : register offset
 * @value : register value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
reg_w32_sdio_8822c(struct halmac_adapter *adapter, u32 offset, u32 value)
{
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	if (adapter->halmac_state.mac_pwr == HALMAC_MAC_POWER_OFF ||
	    (offset & (4 - 1)) !=  0) {
		if ((offset & 0xFFFF0000) == 0 && ((offset & (4 - 1)) == 0)) {
			status = w_indir_sdio_88xx(adapter, offset, value,
						   HALMAC_IO_DWORD);
		} else {
			if ((offset & 0xFFFF0000) == 0)
				offset |= WLAN_IOREG_OFFSET;

			status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
			if (status != HALMAC_RET_SUCCESS) {
				PLTFM_MSG_ERR("[ERR]convert offset\n");
				return status;
			}
			PLTFM_SDIO_CMD52_W(offset, (u8)(value & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 1,
					   (u8)((value >> 8) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 2,
					   (u8)((value >> 16) & 0xFF));
			PLTFM_SDIO_CMD52_W(offset + 3,
					   (u8)((value >> 24) & 0xFF));
		}
	} else {
		if ((offset & 0xFFFF0000) == 0)
			offset |= WLAN_IOREG_OFFSET;

		status = cnv_to_sdio_bus_offset_88xx(adapter, &offset);
		if (status != HALMAC_RET_SUCCESS) {
			PLTFM_MSG_ERR("[ERR]convert offset\n");
			return status;
		}
		PLTFM_SDIO_CMD53_W32(offset, value);
	}

	return status;
}

static enum halmac_ret_status
chk_oqt_8822c(struct halmac_adapter *adapter, u32 tx_agg_num, u8 *buf,
	      u8 macid_cnt)
{
	u32 cnt = 10;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;

	/*S0, S1 are not allowed to use, 0x4E4[0] should be 0. Soar 20160323*/
	/*no need to check non_ac_oqt_number*/
	/*HI and MGQ blocked will cause protocal issue before H_OQT being full*/
	switch ((enum halmac_qsel)GET_TX_DESC_QSEL(buf)) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		if (macid_cnt > WLAN_ACQ_NUM_MAX &&
		    tx_agg_num > (OQT_ENTRY_AC_8822C - 1)) {
			PLTFM_MSG_WARN("[WARN]txagg num %d > oqt entry\n",
				       tx_agg_num);
			PLTFM_MSG_WARN("[WARN]macid cnt %d > acq max\n",
				       macid_cnt);
		}

		cnt = 10;
		do {
			if (fs_info->ac_oqt_num == OQT_ENTRY_AC_8822C &&
			    fs_info->ac_empty >= macid_cnt) {
				fs_info->ac_empty -= macid_cnt;
				break;
			} else if (fs_info->ac_oqt_num > tx_agg_num) {
				fs_info->ac_empty = 0;
				fs_info->ac_oqt_num = 0;
				break;
			}

			update_oqt_free_space_8822c(adapter);

			cnt--;
			if (cnt == 0) {
				PLTFM_MSG_WARN("ac_oqt_num %d, ac_empty %d, tx_agg_num %d, macid_cnt %d\n",
					       fs_info->ac_oqt_num, fs_info->ac_empty, tx_agg_num, macid_cnt);
				return HALMAC_RET_OQT_NOT_ENOUGH;
			}
		} while (1);
		break;
	case HALMAC_QSEL_MGNT:
	case HALMAC_QSEL_HIGH:
		if (tx_agg_num > (OQT_ENTRY_NOAC_8822C - 1))
			PLTFM_MSG_WARN("[WARN]tx_agg_num %d > oqt entry\n",
				       tx_agg_num);
		cnt = 10;
		do {
			if ((fs_info->non_ac_oqt_num > tx_agg_num) &&
			    (fs_info->non_ac_oqt_num == OQT_ENTRY_NOAC_8822C)) {
				fs_info->non_ac_oqt_num -= (u8)tx_agg_num;
				break;
			}

			update_oqt_free_space_8822c(adapter);

			cnt--;
			if (cnt == 0) {
				PLTFM_MSG_WARN("non_ac_oqt_num %d, tx_agg_num %d\n",
					       fs_info->non_ac_oqt_num, tx_agg_num);
				return HALMAC_RET_OQT_NOT_ENOUGH;
			}
		} while (1);
		break;
	default:
		break;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_oqt_free_space_8822c(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;
	u32 oqt_free_page;
	u8 data[8] = {0};

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R)
		HALMAC_REG_SDIO_RN(REG_SDIO_OQT_FREE_TXPG_V1, 8, data);
	else
		HALMAC_REG_SDIO_RN(REG_SDIO_OQT_FREE_TXPG_V1, 5, data);

	oqt_free_page = rtk_le32_to_cpu(*(u32 *)(data));

	fs_info->ac_oqt_num = (u8)BIT_GET_AC_OQT_FREEPG_V1(oqt_free_page);
	fs_info->non_ac_oqt_num = (u8)BIT_GET_NOAC_OQT_FREEPG_V1(oqt_free_page);
	fs_info->ac_empty = 0;

	update_ac_empty_8822c(adapter, data[4]);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_sdio_free_page_8822c(struct halmac_adapter *adapter)
{
	u32 free_page = 0;
	u32 free_page2 = 0;
	u32 free_page3 = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;
	enum halmac_sdio_cmd53_4byte_mode cmd53_4byte =
						adapter->sdio_cmd53_4byte;
	u8 data[16] = {0};

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_RW ||
	    cmd53_4byte == HALMAC_SDIO_CMD53_4BYTE_MODE_R)
		HALMAC_REG_SDIO_RN(REG_SDIO_FREE_TXPG, 16, data);
	else
		HALMAC_REG_SDIO_RN(REG_SDIO_FREE_TXPG, 13, data);

	free_page = rtk_le32_to_cpu(*(u32 *)(data));
	free_page2 = rtk_le32_to_cpu(*(u32 *)(data + 4));
	free_page3 = rtk_le32_to_cpu(*(u32 *)(data + 8));

	fs_info->hiq_pg_num = (u16)BIT_GET_HIQ_FREEPG_V1(free_page);
	fs_info->miq_pg_num = (u16)BIT_GET_MID_FREEPG_V1(free_page);
	fs_info->lowq_pg_num = (u16)BIT_GET_LOW_FREEPG_V1(free_page2);
	fs_info->pubq_pg_num = (u16)BIT_GET_PUB_FREEPG_V1(free_page2);
	fs_info->exq_pg_num = (u16)BIT_GET_EXQ_FREEPG_V1(free_page3);
	fs_info->ac_oqt_num = (u8)BIT_GET_AC_OQT_FREEPG_V1(free_page3);
	fs_info->non_ac_oqt_num = (u8)BIT_GET_NOAC_OQT_FREEPG_V1(free_page3);

	update_ac_empty_8822c(adapter, data[12]);

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
update_ac_empty_8822c(struct halmac_adapter *adapter, u8 value)
{
	struct halmac_sdio_free_space *free_space;

	free_space = &adapter->sdio_fs;
	free_space->ac_empty = 0;

	if (free_space->ac_oqt_num == OQT_ENTRY_AC_8822C) {
		while (value > 0) {
			value = value & (value - 1);
			free_space->ac_empty++;
		}
	} else {
		PLTFM_MSG_TRACE("[TRACE]free_space->ac_oqt_num %d != %d\n",
				free_space->ac_oqt_num, OQT_ENTRY_AC_8822C);
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * phy_cfg_sdio_8822c() - phy config
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
phy_cfg_sdio_8822c(struct halmac_adapter *adapter,
		   enum halmac_intf_phy_platform pltfm)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_pcie_switch_8821c() - pcie gen1/gen2 switch
 * @adapter : the adapter of halmac
 * @cfg : gen1/gen2 selection
 * Author : KaiYuan Chang
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pcie_switch_sdio_8822c(struct halmac_adapter *adapter,
		       enum halmac_pcie_cfg cfg)
{
	return HALMAC_RET_NOT_SUPPORT;
}

/**
 * intf_tun_sdio_8822c() - sdio interface fine tuning
 * @adapter : the adapter of halmac
 * Author : Ivan
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
intf_tun_sdio_8822c(struct halmac_adapter *adapter)
{
	return HALMAC_RET_SUCCESS;
}

/**
 * halmac_get_sdio_tx_addr_sdio_88xx() - get CMD53 addr for the TX packet
 * @adapter : the adapter of halmac
 * @buf : tx packet, include txdesc
 * @size : tx packet size
 * @cmd53_addr : cmd53 addr value
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
get_sdio_tx_addr_8822c(struct halmac_adapter *adapter, u8 *buf, u32 size,
		       u32 *cmd53_addr)
{
	u32 len_unit4, len_unit1, value32;
	u16 block_size = adapter->sdio_hw_info.block_size;
	u8 is_agg_len = 0;
	struct halmac_sdio_hw_info *hw_info = &adapter->sdio_hw_info;
	enum halmac_qsel queue_sel;
	enum halmac_dma_mapping dma_mapping;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (!buf) {
		PLTFM_MSG_ERR("[ERR]buf is NULL!!\n");
		return HALMAC_RET_DATA_BUF_NULL;
	}

	if (size == 0) {
		PLTFM_MSG_ERR("[ERR]size is 0!!\n");
		return HALMAC_RET_DATA_SIZE_INCORRECT;
	}

	queue_sel = (enum halmac_qsel)GET_TX_DESC_QSEL(buf);

	switch (queue_sel) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VO];
		break;
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VI];
		break;
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BE];
		break;
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BK];
		break;
	case HALMAC_QSEL_MGNT:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_MG];
		break;
	case HALMAC_QSEL_HIGH:
	case HALMAC_QSEL_BCN:
	case HALMAC_QSEL_CMD:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_HI];
		break;
	default:
		PLTFM_MSG_ERR("[ERR]Qsel is out of range\n");
		return HALMAC_RET_QSEL_INCORRECT;
	}

	len_unit4 = (size >> 2) + ((size & (4 - 1)) ? 1 : 0);
	len_unit1 = (len_unit4 << 2);

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_HIGH;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_NORMAL;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_LOW;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*cmd53_addr = HALMAC_SDIO_CMD_ADDR_TXFF_EXTRA;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	if (hw_info->tx_addr_format == HALMAC_SDIO_AGG_MODE ||
	    (hw_info->tx_addr_format == HALMAC_SDIO_DUMMY_AUTO_MODE &&
	     len_unit1 < block_size)) {
		is_agg_len = 1;
	} else if (hw_info->tx_addr_format == HALMAC_SDIO_DUMMY_AUTO_MODE &&
		   len_unit1 == block_size) {
		if (hw_info->tx_512_by_byte_mode == 0)
			is_agg_len = 0;
		else
			is_agg_len = 1;
	} else if (hw_info->tx_addr_format == HALMAC_SDIO_DUMMY_BLOCK_MODE ||
		   (hw_info->tx_addr_format == HALMAC_SDIO_DUMMY_AUTO_MODE &&
		    len_unit1 > block_size)) {
			is_agg_len = 0;
	} else {
		PLTFM_MSG_ERR("[ERR]tx_addr_format is undefined\n");
		return HALMAC_RET_NOT_SUPPORT;
	}

	if (is_agg_len == 0) {
		value32 = len_unit1 % block_size;
		if (value32)
			value32 = (block_size - value32) >> 2;
		*cmd53_addr = hw_info->tx_seq | (*cmd53_addr << 13) |
				((value32 & HALMAC_SDIO_4BYTE_LEN_MASK) << 1);
		hw_info->tx_seq = ~hw_info->tx_seq & 0x01;
	} else {
		*cmd53_addr = (*cmd53_addr << 13) |
				(len_unit4 & HALMAC_SDIO_4BYTE_LEN_MASK);
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_qsel_8822c(struct halmac_adapter *adapter, u8 qsel_first, u8 *pkt,
	       u8 *macid_cnt)
{
	u8 flag = 0;
	u8 qsel_now;
	u8 macid;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;

	macid = (u8)GET_TX_DESC_MACID(pkt);
	qsel_now = (u8)GET_TX_DESC_QSEL(pkt);
	if (qsel_first == qsel_now) {
		if (*(fs_info->macid_map + macid) == 0) {
			*(fs_info->macid_map + macid) = 1;
			(*macid_cnt)++;
		}
	} else {
		switch ((enum halmac_qsel)qsel_now) {
		case HALMAC_QSEL_VO:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VO_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_VO_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VO)
				flag = 1;
			break;
		case HALMAC_QSEL_VI:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VI_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_VI_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_VI)
				flag = 1;
			break;
		case HALMAC_QSEL_BE:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BE_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_BE_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BE)
				flag = 1;
			break;
		case HALMAC_QSEL_BK:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BK_V2)
				flag = 1;
			break;
		case HALMAC_QSEL_BK_V2:
			if ((enum halmac_qsel)qsel_first != HALMAC_QSEL_BK)
				flag = 1;
			break;
		case HALMAC_QSEL_MGNT:
		case HALMAC_QSEL_HIGH:
		case HALMAC_QSEL_BCN:
		case HALMAC_QSEL_CMD:
			flag = 1;
			break;
		default:
			PLTFM_MSG_ERR("[ERR]Qsel is out of range\n");
			return HALMAC_RET_QSEL_INCORRECT;
		}
		if (flag == 1) {
			PLTFM_MSG_ERR("[ERR]Multi-Qsel is not allowed\n");
			PLTFM_MSG_ERR("[ERR]qsel = %d, %d\n",
				      qsel_first, qsel_now);
			return HALMAC_RET_QSEL_INCORRECT;
		}
		if (*(fs_info->macid_map + macid + MACID_MAX_8822C) == 0) {
			*(fs_info->macid_map + macid + MACID_MAX_8822C) = 1;
			(*macid_cnt)++;
		}
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_dma_mapping_8822c(struct halmac_adapter *adapter, u16 **cur_fs,
		      u8 qsel_first)
{
	enum halmac_dma_mapping dma_mapping;

	switch ((enum halmac_qsel)qsel_first) {
	case HALMAC_QSEL_VO:
	case HALMAC_QSEL_VO_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VO];
		break;
	case HALMAC_QSEL_VI:
	case HALMAC_QSEL_VI_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_VI];
		break;
	case HALMAC_QSEL_BE:
	case HALMAC_QSEL_BE_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BE];
		break;
	case HALMAC_QSEL_BK:
	case HALMAC_QSEL_BK_V2:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_BK];
		break;
	case HALMAC_QSEL_MGNT:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_MG];
		break;
	case HALMAC_QSEL_HIGH:
		dma_mapping = adapter->pq_map[HALMAC_PQ_MAP_HI];
		break;
	case HALMAC_QSEL_BCN:
	case HALMAC_QSEL_CMD:
		*cur_fs = &adapter->sdio_fs.hiq_pg_num;
		return HALMAC_RET_SUCCESS;
	default:
		PLTFM_MSG_ERR("[ERR]Qsel is out of range: %d\n", qsel_first);
		return HALMAC_RET_QSEL_INCORRECT;
	}

	switch (dma_mapping) {
	case HALMAC_DMA_MAPPING_HIGH:
		*cur_fs = &adapter->sdio_fs.hiq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_NORMAL:
		*cur_fs = &adapter->sdio_fs.miq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_LOW:
		*cur_fs = &adapter->sdio_fs.lowq_pg_num;
		break;
	case HALMAC_DMA_MAPPING_EXTRA:
		*cur_fs = &adapter->sdio_fs.exq_pg_num;
		break;
	default:
		PLTFM_MSG_ERR("[ERR]DmaMapping is out of range\n");
		return HALMAC_RET_DMA_MAP_INCORRECT;
	}

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
chk_rqd_page_num_8822c(struct halmac_adapter *adapter, u8 *buf, u32 *rqd_pg_num,
		       u16 **cur_fs, u8 *macid_cnt, u32 tx_agg_num)
{
	u8 *pkt;
	u8 qsel_first;
	u32 i, pkt_size;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	struct halmac_sdio_free_space *fs_info = &adapter->sdio_fs;

	pkt = buf;

	qsel_first = (u8)GET_TX_DESC_QSEL(pkt);

	status = chk_dma_mapping_8822c(adapter, cur_fs, qsel_first);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	if (!fs_info->macid_map) {
		PLTFM_MSG_ERR("[ERR]halmac allocate Macid_map Fail!!\n");
		return HALMAC_RET_MALLOC_FAIL;
	}

	PLTFM_MEMSET(fs_info->macid_map, 0x00, fs_info->macid_map_size);

	for (i = 0; i < tx_agg_num; i++) {
		/*QSEL parser*/
		status = chk_qsel_8822c(adapter, qsel_first, pkt, macid_cnt);
		if (status != HALMAC_RET_SUCCESS)
			return status;

		/*Page number parser*/
		pkt_size = GET_TX_DESC_TXPKTSIZE(pkt) + GET_TX_DESC_OFFSET(pkt);
		*rqd_pg_num += (pkt_size >> TX_PAGE_SIZE_SHIFT_88XX) +
				((pkt_size & (TX_PAGE_SIZE_88XX - 1)) ? 1 : 0);

		pkt += HALMAC_ALIGN(GET_TX_DESC_TXPKTSIZE(pkt) +
				    (GET_TX_DESC_PKT_OFFSET(pkt) << 3) +
				    TX_DESC_SIZE_88XX, 8);
	}

	return HALMAC_RET_SUCCESS;
}

u32
get_sdio_int_lat_8822c(struct halmac_adapter *adapter)
{
	u32 free_cnt, free_cnt2;
	u32 int_start;
	u32 int_lat = 0;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	int_start = HALMAC_REG_R32(REG_SDIO_MONITOR);
	free_cnt = HALMAC_REG_R32(REG_FREERUN_CNT);
	free_cnt2 = HALMAC_REG_R32(REG_FREERUN_CNT);
	int_lat = free_cnt - int_start - (free_cnt2 - free_cnt);

	return int_lat;
}

enum halmac_ret_status
get_sdio_clk_cnt_8822c(struct halmac_adapter *adapter, u32 *cnt)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u8 value8;

	value8 = HALMAC_REG_R8(REG_SDIO_MONITOR_2 + 2);
	if ((value8 & (BIT(5) | BIT(6))) == 0)
		*cnt = HALMAC_REG_R32(REG_SDIO_MONITOR_2) & 0x1FFFFF;
	else
		status = HALMAC_RET_FAIL;

	return status;
}

enum halmac_ret_status
set_sdio_wt_en_8822c(struct halmac_adapter *adapter, u8 enable)
{
	u32 reg = REG_SDIO_MONITOR_2 + 2;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	if (enable == 1)
		PLTFM_SDIO_CMD52_W(reg, BIT(7));
	else
		PLTFM_SDIO_CMD52_W(reg, 0);

	return status;
}

enum halmac_ret_status
set_sdio_clk_mon_8822c(struct halmac_adapter *adapter,
		       enum halmac_sdio_clk_monitor monitor)
{
	u32 reg = REG_SDIO_MONITOR_2 + 2;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	status = cnv_to_sdio_bus_offset_88xx(adapter, &reg);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	set_sdio_wt_en_8822c(adapter, 0);

	switch (monitor) {
	case HALMAC_MONITOR_5US:
		PLTFM_SDIO_CMD52_W(reg, BIT(5));
		break;
	case HALMAC_MONITOR_50US:
		PLTFM_SDIO_CMD52_W(reg, BIT(6));
		break;
	case HALMAC_MONITOR_9MS:
		PLTFM_SDIO_CMD52_W(reg, BIT(5) | BIT(6));
		break;
	default:
		break;
	}

	return status;
}

#endif /* HALMAC_8822C_SUPPORT*/
