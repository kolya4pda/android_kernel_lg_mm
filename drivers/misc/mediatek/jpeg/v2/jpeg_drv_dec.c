#ifdef JPEG_DEC_DRIVER

#include <linux/kernel.h>
#include <linux/xlog.h>

#define JPEG_MSG pr_debug
#define JPEG_WRN pr_debug
#define JPEG_ERR pr_debug
#define JPEG_VEB pr_debug

#include "jpeg_drv_common.h"
#include "jpeg_drv_reg.h"

/* #define DUMP_REG_CMD */

#define ALIGN_MASK(BIT)		(((unsigned int)(BIT) >> 3) - 1)

#define CHECK_ALIGN(value, align, addr)  \
{ \
	if (value & (align-1)) \
		JPEG_WRN("WriteREG: Try to write %d to REG(%x) without %d align!!\n ", value, addr, align); \
}

#define TEST_JPEG_DEBUG_EN

/* unsigned int _jpeg_dec_int_status = 0; */
unsigned int _jpeg_dec_dump_reg_en = 0;
kal_uint32 _jpeg_dec_int_status = 0;
kal_uint32 _jpeg_dec_mode = 0;


int jpeg_isr_dec_lisr(void)
{
	unsigned int tmp = 0, tmp1 = 0;

	tmp1 = REG_JPGDEC_INTERRUPT_STATUS;
	tmp = tmp1 & BIT_INQST_MASK_ALLIRQ;

	if (tmp) {
		_jpeg_dec_int_status = tmp;

		/* error handling, since irq can't be cleared by register. we need to mask the irq out */
		if (((_jpeg_dec_int_status & BIT_INQST_MASK_ERROR_BS) == BIT_INQST_MASK_ERROR_BS) ||
		    ((_jpeg_dec_int_status & BIT_INQST_MASK_OVERFLOW) == BIT_INQST_MASK_OVERFLOW)) {
			IMG_REG_WRITE(0x0, REG_ADDR_JPGDEC_IRQ_EN);
			return 0;
		}

		if (_jpeg_dec_mode == 1) {
#ifndef JPEG_DEC_IRQ_ENHANCE
			/* always not clear, but mask jpeg interrupt temporarily when EOF irq have not rise yet */
			/* this is workaround for HW decoder ROW mode since clear pause irq will trigger HW to fire */
			/* we need to disable jpeg irq before resume to avoid jpeg irq rise too frequenct */
			if (((_jpeg_dec_int_status & BIT_INQST_MASK_EOF) != BIT_INQST_MASK_EOF) &&
			    ((_jpeg_dec_int_status & BIT_INQST_MASK_PAUSE) == BIT_INQST_MASK_PAUSE))
				IMG_REG_WRITE(0x0, REG_ADDR_JPGDEC_IRQ_EN);
			/* clear jpeg decode interrupt if it is not the situiation that only pause irq was raised */
			else
				IMG_REG_WRITE(tmp, REG_ADDR_JPGDEC_INTERRUPT_STATUS);
#else
			IMG_REG_WRITE(tmp, REG_ADDR_JPGDEC_INTERRUPT_STATUS);
#endif
			return 0;
		} else {
			/* / clear the interrupt status register */
			IMG_REG_WRITE(tmp, REG_ADDR_JPGDEC_INTERRUPT_STATUS);

			return 0;
		}
	}
	return -1;
}


void jpeg_drv_dec_start(void)
{
#ifndef JPEG_DEC_IRQ_ENHANCE
	JPEG_MSG("JPGDC jpeg_drv_dec_start:  unmask jpeg irq!\n");
	/* unmask jpeg irq */
	IMG_REG_WRITE(0x37, REG_ADDR_JPGDEC_IRQ_EN);
#else
	unsigned int reg_value;

	JPEG_MSG("JPGDC jpeg_drv_dec_start: JPEG_DEC_IRQ_ENHANCE!\n");
	IMG_REG_READ(reg_value, REG_ADDR_JPGDEC_IRQ_EN);
	reg_value |= BIT_DEC_IRQ_EN_DEBUG_BRP_FLAG;

	IMG_REG_WRITE(reg_value, REG_ADDR_JPGDEC_IRQ_EN);
#endif

	IMG_REG_WRITE(0, REG_ADDR_JPGDEC_TRIG);	/* REG_JPGDEC_TRIG = 0; */
}


/**
 * Call this function to reset the JPEG decoder.
 */
/* workaround for jpeg odd read operation at cg gating state */
void jpeg_drv_dec_verify_state_and_reset(void)
{
	unsigned int temp, value;

	IMG_REG_WRITE(0x00, REG_ADDR_JPGDEC_RESET);
	IMG_REG_WRITE(0x00, REG_ADDR_JPGDEC_RESET);
	IMG_REG_WRITE(0x10, REG_ADDR_JPGDEC_RESET);
	IMG_REG_WRITE(0x10, REG_ADDR_JPGDEC_RESET);

	IMG_REG_READ(temp, REG_ADDR_JPGDEC_ULTRA_THRES);
	IMG_REG_READ(value, REG_ADDR_JPGDEC_FILE_ADDR);

	/* issue happen, need to do 1 read at cg gating state */
	if (value == 0xFFFFFFFF) {
		JPEG_MSG("JPGDEC APB R/W issue found, start to do recovery!\n");
		jpeg_drv_dec_power_off();
		IMG_REG_READ(value, REG_ADDR_JPGDEC_ULTRA_THRES);
		jpeg_drv_dec_power_on();
	}

	_jpeg_dec_int_status = 0;
	_jpeg_dec_mode = 0;
}


void jpeg_drv_dec_warm_reset(void)
{
	unsigned int int_status, i, value;

	int_status = REG_JPGDEC_INTERRUPT_STATUS;

	/* clear all interrupts except pause interrupt since clear this interrupt will cause HW to trigger */
	IMG_REG_WRITE(0x000000EF, REG_ADDR_JPGDEC_INTERRUPT_STATUS);	/* ack decoder */

	/* we need to wait GMC idle only when EOF irq does not issue */
	if (!(int_status & BIT_INQST_MASK_EOF)) {
		IMG_REG_READ(value, REG_ADDR_JPGDEC_DU_CTRL);
		IMG_REG_WRITE((value & 0xBFFFFFFF), REG_ADDR_JPGDEC_DU_CTRL);	/* dsiable GMC */

		/* wait GMC idle */
		i = 5000;
		while (i > 0) {
			/* break if GMC idle is 1 */
			if (((REG_JPGDEC_STATUS) & 0x08000000))
				break;

			i--;
		}

		if (i == 0) {
			/* dump key register */
			JPEG_WRN
			("[JPEGDRV] jpeg_dec_reset timeout!! 0x278 = 0x%x, 0x23c = 0x%x, 0x184 = 0x%x\n",
			REG_JPGDEC_STATUS, REG_JPGDEC_DU_CTRL, REG_JPGDEC_DEBUG1);
			JPEG_WRN
			("[JPEGDRV] 0x304 = 0x%x, 0x308 = 0x%x, 0x30c = 0x%x, 0x310 = 0x%x\n",
			REG_JPGDEC_SMI_DEBUG0, REG_JPGDEC_SMI_DEBUG1, REG_JPGDEC_SMI_DEBUG2,
			REG_JPGDEC_SMI_DEBUG3);
		}
	}

	IMG_REG_WRITE(0x00, REG_ADDR_JPGDEC_RESET);
	IMG_REG_WRITE(0x10, REG_ADDR_JPGDEC_RESET);
	/* REG_JPGDEC_RESET = 0x00; */

	_jpeg_dec_int_status = 0;
	_jpeg_dec_mode = 0;
}


void jpeg_drv_dec_reset(void)
{

	IMG_REG_WRITE(0x00, REG_ADDR_JPGDEC_RESET);
	IMG_REG_WRITE(0x10, REG_ADDR_JPGDEC_RESET);
	/* REG_JPGDEC_RESET = 0x00; */

	_jpeg_dec_int_status = 0;
	_jpeg_dec_mode = 0;

}


void wait_pr(void)
{
	unsigned int timeout1 = 0xF;
	unsigned int timeout2 = 0xFFFFF;
	unsigned int timeout3 = 0xFFFFFFF;


	while (timeout1 > 0) {
		while (timeout2 > 0) {
			while (timeout3 > 0)
				timeout3--;

			timeout2--;
		}
		timeout1--;
	}
}

void jpeg_drv_dec_set_brz_factor(unsigned char yHScale, unsigned char yVScale,
				 unsigned char cbcrHScale, unsigned char cbcrVScale)
{
	unsigned int u4Value;


	/* yHScale =  yHScale; */
	/* yVScale =  yVScale; */
	/* cbcrHScale = cbcrHScale; */
	/* cbcrVScale = cbcrVScale; */
#if 0
	if (srcFormat == JPG_COLOR_444 ||
	    srcFormat == JPG_COLOR_422V || srcFormat == JPG_COLOR_422Vx2) {

		cbcrHScale++;
	}
#endif
	u4Value = (cbcrVScale << BIT_BRZ_CV_SHIFT) | (cbcrHScale << BIT_BRZ_CH_SHIFT) |
	    (yVScale << BIT_BRZ_YV_SHIFT) | (yHScale << BIT_BRZ_YH_SHIFT);

	IMG_REG_WRITE(u4Value, REG_ADDR_JPGDEC_BRZ_FACTOR);

}


void jpeg_drv_dec_set_dst_bank0(unsigned int addr_Y, unsigned int addr_U, unsigned int addr_V)
{


	IMG_REG_WRITE(addr_Y, REG_ADDR_JPGDEC_DEST_ADDR0_Y);
	IMG_REG_WRITE(addr_U, REG_ADDR_JPGDEC_DEST_ADDR0_U);
	IMG_REG_WRITE(addr_V, REG_ADDR_JPGDEC_DEST_ADDR0_V);

}


void jpeg_drv_dec_set_dst_bank1(unsigned int addr_Y, unsigned int addr_U, unsigned int addr_V)
{
/* unsigned int u4Value; */

	IMG_REG_WRITE(addr_Y, REG_ADDR_JPGDEC_DEST_ADDR1_Y);
	IMG_REG_WRITE(addr_U, REG_ADDR_JPGDEC_DEST_ADDR1_U);
	IMG_REG_WRITE(addr_V, REG_ADDR_JPGDEC_DEST_ADDR1_V);

}


int jpeg_drv_dec_set_memStride(unsigned int CompMemStride_Y, unsigned int CompMemStride_UV)
{

	IMG_REG_WRITE((CompMemStride_Y & 0xFFFF), REG_ADDR_JPGDEC_STRIDE_Y);
	IMG_REG_WRITE((CompMemStride_UV & 0xFFFF), REG_ADDR_JPGDEC_STRIDE_UV);

	return (int)E_HWJPG_OK;
}


int jpeg_drv_dec_set_imgStride(unsigned int CompStride_Y, unsigned int CompStride_UV)
{
/* unsigned int u4Reg; */

	IMG_REG_WRITE((CompStride_Y & 0xFFFF), REG_ADDR_JPGDEC_IMG_STRIDE_Y);
	IMG_REG_WRITE((CompStride_UV & 0xFFFF), REG_ADDR_JPGDEC_IMG_STRIDE_UV);

	return (int)E_HWJPG_OK;
}


void jpeg_drv_dec_set_pause_mcu_idx(unsigned int McuIdx)
{

	IMG_REG_WRITE((McuIdx & 0x0003FFFFFF), REG_ADDR_JPGDEC_PAUSE_MCU_NUM);

}


void jpeg_drv_dec_set_dec_mode(int i4DecMode)
{
	unsigned int u4Value = i4DecMode;

	/* 0: full frame, 1: direct couple mode, 2: pause/resume mode, 3: Reserved */

	if (u4Value > 0x02)
		JPEG_WRN("Warning : try to set invalid decode mode, %d!!\n", u4Value);
	IMG_REG_WRITE((u4Value & 0x03), REG_ADDR_JPGDEC_OPERATION_MODE);

}

void jpeg_drv_dec_set_debug_mode(void)
{
	unsigned int u4Value;

	u4Value = REG_JPGDEC_DEBUG_MODE;
	u4Value |= 0x80000000;
	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_DEBUG_MODE);
}


void jpeg_drv_dec_set_bs_writePtr(unsigned int writePtr)
{
	CHECK_ALIGN(writePtr, 16, (kal_uint32) REG_ADDR_JPGDEC_FILE_BRP);

	IMG_REG_WRITE((writePtr), REG_ADDR_JPGDEC_FILE_BRP);

}


void jpeg_drv_dec_set_bs_info(unsigned int bsBase, unsigned int bsSize)
{
	CHECK_ALIGN(bsBase, 16, (kal_uint32) REG_ADDR_JPGDEC_FILE_ADDR);
	CHECK_ALIGN(bsSize, 128, (kal_uint32) REG_ADDR_JPGDEC_FILE_TOTAL_SIZE);

	IMG_REG_WRITE((bsBase), REG_ADDR_JPGDEC_FILE_ADDR);

	IMG_REG_WRITE((bsSize), REG_ADDR_JPGDEC_FILE_TOTAL_SIZE);
}


/* void jpeg_drv_dec_set_total_bs_size_align128(unsigned int bsSize) */
/* { */
/* unsigned int u4tmp; */
/*  */
/* if(bsSize & 127){ */
/* u4tmp = bsSize & (~127) ; */
/* }else */
/* u4tmp = bsSize ; */
/* REG_JPGDEC_FILE_TOTAL_SIZE  =  u4tmp; */
/*  */
/*  */
/* } */


void jpeg_drv_dec_set_comp_id(unsigned int Y_ID, unsigned int U_ID, unsigned int V_ID)
{
	unsigned int u4Value;

	u4Value = ((Y_ID & 0x00FF) << 24) | ((U_ID & 0x00FF) << 16) | ((V_ID & 0x00FF) << 8);
	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_COMP_ID);

}

void jpeg_drv_dec_set_total_mcu(unsigned int TotalMcuNum)
{
	unsigned int u4Value;

	u4Value = TotalMcuNum - 1;
	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_TOTAL_MCU_NUM);

}


void jpeg_drv_dec_set_comp0_du(unsigned int GrayDuNum)
{
	unsigned int u4Value;

	u4Value = GrayDuNum - 1;
	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_COMP0_DATA_UNIT_NUM);

}


void jpeg_drv_dec_set_du_membership(unsigned int u4Membership, unsigned int GMC_en,
				    unsigned int IsGray)
{
#if 0
	/* u4Membership = u4Membership | 0x3FFFFF24 ;//111_111_111_111_111_111_111_100_100_100 */
	u4Membership = u4Membership | 0x24924924;	/* 100_100_100_100_100_100_100_100_100_100 */
#endif

	if (IsGray)
		u4Membership = (IsGray << 31) | (GMC_en << 30) | 0x3FFFFFFC;
	else
		u4Membership = (IsGray << 31) | (GMC_en << 30) | u4Membership;
	IMG_REG_WRITE((u4Membership), REG_ADDR_JPGDEC_DU_CTRL);	/* REG_JPGDEC_DU_CTRL = u4Membership ; */

}


/* set q table for each component */
void jpeg_drv_dec_set_q_table(kal_uint32 id0, kal_uint32 id1, kal_uint32 id2)
{
	unsigned int u4Value;

	u4Value = ((id0 & 0x0f) << 8) | ((id1 & 0x0f) << 4) | ((id2 & 0x0f) << 0);

	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_QT_ID);
#ifdef DUMP_REG_CMD
	JPEG_WRN("WriteREG(VLD_REG_OFST , 32'h%08x);\n",
		 ((id0 & 0x0f) << 8) | ((id1 & 0x0f) << 4) | ((id2 & 0x0f) << 0));
#endif
}


unsigned int jpeg_drv_dec_get_irqState(void)
{
	unsigned int u4Value;

	u4Value = REG_JPGDEC_INTERRUPT_STATUS;
	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_INTERRUPT_STATUS);
	return u4Value;
}

unsigned int jpeg_drv_dec_get_decState(void)
{
	unsigned int u4Value;

	u4Value = REG_JPGDEC_STATUS;
	JPEG_MSG("JPED_DEC_DRV: STATUS %x!!\n", u4Value);
	return u4Value;
}


unsigned int jpeg_drv_dec_get_decMCU(void)
{
	unsigned int u4Value;

	u4Value = REG_JPGDEC_MCU_CNT;
	JPEG_MSG("JPED_DEC_DRV: MCU_CNT %x!!\n", u4Value);
	return u4Value;
}




void jpeg_drv_dec_set_dma_group(unsigned int McuInGroup, unsigned int GroupNum,
				unsigned int LastMcuNum)
{
	unsigned int McuInGroup_1 = McuInGroup - 1;
	unsigned int GroupNum_1 = GroupNum - 1;
	unsigned int LastMcuNum_1 = LastMcuNum - 1;
	unsigned int u4Value;

	u4Value =
	    ((McuInGroup_1 & 0x00FF) << 16) | ((GroupNum_1 & 0x007F) << 8) | (LastMcuNum_1 &
									      0x00FF);

	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_WDMA_CTRL);	/*  */

}




void jpeg_drv_dec_set_sampling_factor(unsigned int compNum, unsigned int u4Y_H, unsigned int u4Y_V,
				      unsigned int u4U_H, unsigned int u4U_V, unsigned int u4V_H,
				      unsigned int u4V_V)
{
	unsigned int u4Value = 0;
	unsigned int u4Y_HV = (DUNUM_MAPPING(u4Y_H) << 2) | DUNUM_MAPPING(u4Y_V);
	unsigned int u4U_HV = (DUNUM_MAPPING(u4U_H) << 2) | DUNUM_MAPPING(u4U_V);
	unsigned int u4V_HV = (DUNUM_MAPPING(u4V_H) << 2) | DUNUM_MAPPING(u4V_V);

	if (compNum == 1)
		u4Value = 0;	/* u4Y_HV << 8; */
	else
		u4Value = (u4Y_HV << 8) | (u4U_HV << 4) | u4V_HV;

	IMG_REG_WRITE((u4Value), REG_ADDR_JPGDEC_DU_SAMPLE);	/* REG_JPGDEC_DU_SAMPLE = u4Value; */

}


int jpeg_drv_dec_set_config_data(JPEG_DEC_DRV_IN *config)
{
	jpeg_drv_dec_set_sampling_factor(config->componentNum,
					 config->hSamplingFactor[0], config->vSamplingFactor[0],
					 config->hSamplingFactor[1], config->vSamplingFactor[1],
					 config->hSamplingFactor[2], config->vSamplingFactor[2]);

	/* set BRZ factor */
	jpeg_drv_dec_set_brz_factor(config->lumaHorDecimate, config->lumaVerDecimate,
				    config->cbcrHorDecimate, config->cbcrVerDecimate);

	/* set group DMA */
	jpeg_drv_dec_set_dma_group(config->dma_McuInGroup, config->dma_GroupNum,
				   config->dma_LastMcuNum);

	/* set componet ID */
	jpeg_drv_dec_set_comp_id(config->componentID[0], config->componentID[1],
				 config->componentID[2]);


	/* set BLK membership */
	jpeg_drv_dec_set_du_membership(config->membershipList, config->gmcEn,
				       (config->componentNum == 1) ? 1 : 0);

	/* set q table id */
	jpeg_drv_dec_set_q_table(config->qTableSelector[0], config->qTableSelector[1],
				 config->qTableSelector[2]);

	/* set dst image stride  */
	jpeg_drv_dec_set_imgStride(config->compImgStride[0], config->compImgStride[1]);

	/* set dst Memory stride  */
	/* if( config->pauseRow_en ){ */
	/* jpeg_drv_dec_set_memStride(config->compTileBufStride[0], config->compTileBufStride[1]); */
	/* }else{ */
	jpeg_drv_dec_set_memStride(config->compMemStride[0], config->compMemStride[1]);
	/* } */

	/* set total MCU number */
	jpeg_drv_dec_set_total_mcu(config->totalMCU);

	/* set Gray DU number */
	jpeg_drv_dec_set_comp0_du(config->comp0_DU);

	/* set pause MCU index */
	jpeg_drv_dec_set_pause_mcu_idx(config->pauseMCU - 1);

	/* set bitstream base, size */
	JPEG_MSG("[JPEGDRV] mode %d, Buf Base 0x%08x, Limit 0x%08x, Size 0x%08x!!\n",
		 config->reg_OpMode, config->srcStreamAddrBase, config->srcStreamAddrWritePtr,
		 config->srcStreamSize);
	jpeg_drv_dec_set_bs_info(config->srcStreamAddrBase, config->srcStreamSize);

	/* set bitstream write pointer */
	jpeg_drv_dec_set_bs_writePtr(config->srcStreamAddrWritePtr);

	/* set Decode Operation Mode */
	jpeg_drv_dec_set_dec_mode(config->reg_OpMode);	/* set full frame or pause/resume */


	/* output bank 0 */
	jpeg_drv_dec_set_dst_bank0(config->outputBuffer0[0], config->outputBuffer0[1],
				   config->outputBuffer0[2]);

	/* output bank 1 */
	jpeg_drv_dec_set_dst_bank1(config->outputBuffer1[0], config->outputBuffer1[1],
				   config->outputBuffer1[2]);


#ifdef TEST_JPEG_DEBUG_EN
	jpeg_drv_dec_set_debug_mode();
#endif

	return (int)E_HWJPG_OK;
}


void jpeg_drv_dec_resume(unsigned int resume)
{

	_jpeg_dec_int_status = 0;
#ifndef JPEG_DEC_IRQ_ENHANCE
	IMG_REG_WRITE((resume), REG_ADDR_JPGDEC_INTERRUPT_STATUS);

	/* unmask jpeg interrupt since we've masked it out when pause irq rise */
	IMG_REG_WRITE(0x37, REG_ADDR_JPGDEC_IRQ_EN);
#else
	IMG_REG_WRITE(0, REG_ADDR_JPGDEC_TRIG);
#endif
}

int jpeg_drv_dec_wait_one_row(JPEG_DEC_DRV_IN *config)
{
	unsigned int timeout = 0x2FFFFF;
	unsigned int irq_status;

	unsigned int tri_cnt = ++config->pauseRowCnt;	/* 1; */
	unsigned int MCU_cnt = 0;
	unsigned int base_Y = config->buffer_Y_PA;	/* 0x89080000; */
	unsigned int base_CB = config->buffer_Cb_PA;	/* 0x89040000; */
	unsigned int base_CR = config->buffer_Cr_PA;	/* 0x89100000; */
	unsigned int ring_row_index = tri_cnt % config->tileBufRowNum;

	/* for( tri_cnt = 1 ; tri_cnt <= 60 ; tri_cnt++) */
	{
		/* wait done */
		if (config->decodeMode == JPEG_DEC_MODE_MCU_ROW) {
			while ((REG_JPGDEC_INTERRUPT_STATUS & BIT_INQST_MASK_ALLIRQ) == 0) {
				timeout--;
				if (timeout == 0)
					break;
			}
		} else {
			while ((REG_JPGDEC_INTERRUPT_STATUS & BIT_INQST_MASK_ALLIRQ) == 0) {
				timeout--;
				if (timeout == 0)
					break;
			}

		}
		irq_status = REG_JPGDEC_INTERRUPT_STATUS;

		MCU_cnt = config->mcuPerRow * (tri_cnt + 1);
		/* MCU_cnt = config->u4McuNumInRow * (tri_cnt+1) ; */

		JPEG_MSG
		    ("JPEG_DEC_WAIT_DONE: tri_cnt %d, irq %x, MCUinRow %d, p_idx %d, %x %x %x!!\n",
		     tri_cnt, irq_status, config->mcuPerRow, MCU_cnt,
		     base_Y + ring_row_index * (config->buffer_Y_row_size),
		     base_CB + ring_row_index * (config->buffer_C_row_size),
		     base_CR + ring_row_index * (config->buffer_C_row_size));

		jpeg_drv_dec_set_dst_bank0(base_Y + ring_row_index * (config->buffer_Y_row_size),
					   base_CB + ring_row_index * (config->buffer_C_row_size),
					   base_CR + ring_row_index * (config->buffer_C_row_size));

		jpeg_drv_dec_set_pause_mcu_idx(MCU_cnt - 1);

		IMG_REG_WRITE((irq_status), REG_ADDR_JPGDEC_INTERRUPT_STATUS);

		    /* Debug: jpeg_drv_dec_dump_reg(); */
		    if (timeout == 0) {
			JPEG_ERR("Error! Decode Timeout.\n");
			jpeg_drv_dec_dump_reg();
			return 0;
		}

		JPEG_ERR("JPEG Decode Success, st %x!!\n", irq_status);
	}
	return 1;
}


int jpeg_drv_dec_wait(JPEG_DEC_DRV_IN *config)
{
	unsigned int timeout = 0x2FFFFF;
	unsigned int irq_status;
	/* wait done */
	if (config->decodeMode == JPEG_DEC_MODE_MCU_ROW) {
		while ((REG_JPGDEC_INTERRUPT_STATUS & BIT_INQST_MASK_ALLIRQ) == 0) {
			timeout--;
			if (timeout == 0)
				break;
		}
	} else {
		while ((REG_JPGDEC_INTERRUPT_STATUS & BIT_INQST_MASK_ALLIRQ) == 0) {
			timeout--;
			if (timeout == 0)
				break;
		}

	}
	irq_status = REG_JPGDEC_INTERRUPT_STATUS;

	IMG_REG_WRITE((irq_status), REG_ADDR_JPGDEC_INTERRUPT_STATUS);

	    /* Debug: jpeg_drv_dec_dump_reg(); */
	    if (timeout == 0) {
		JPEG_ERR("Error! Decode Timeout.\n");
		jpeg_drv_dec_dump_reg();
		return 0;
	}

	JPEG_ERR("JPEG Decode Success, st %x!!\n", irq_status);
	return 1;
}

kal_uint32 jpeg_drv_dec_get_result(void)
{

	JPEG_MSG("[JPEGDRV] get_result mode %x, irq_sts %x!!\n", _jpeg_dec_mode,
	       _jpeg_dec_int_status);
	/* if(_jpeg_dec_mode == 1){ */
	/* if(_jpeg_dec_int_status & BIT_INQST_MASK_END ) */
	/* REG_JPGDEC_INTERRUPT_STATUS = _jpeg_dec_int_status ; */
	/* } */

	if (_jpeg_dec_int_status & BIT_INQST_MASK_EOF)
		return 0;
	else if (_jpeg_dec_int_status & BIT_INQST_MASK_PAUSE)
		return 1;
	else if (_jpeg_dec_int_status & BIT_INQST_MASK_UNDERFLOW)
		return 2;
	else if (_jpeg_dec_int_status & BIT_INQST_MASK_OVERFLOW)
		return 3;
	else if (_jpeg_dec_int_status & BIT_INQST_MASK_ERROR_BS)
		return 4;

	return 5;
}

int jpeg_drv_dec_break(void)
{
	unsigned int timeout = 0xFFFFF;

	jpeg_drv_dec_warm_reset();

	while (((REG_JPGDEC_STATUS & BIT_DEC_ST_STATE_MASK) == BIT_DEC_ST_STATE_IDLE)) {
		timeout--;
		if (timeout == 0)
			break;
	}
	if (timeout == 0)
		return -1;
	return 0;
}

void jpeg_drv_dec_dump_key_reg(void)
{
	unsigned int reg_value = 0;
	unsigned int index = 0;

	JPEG_WRN("<<<<<= JPEG DEC DUMP KEY =>>>>>\n");
	/* bank0, bank1 address */
	for (index = 0x140; index <= 0x154; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
	/* pause index */
	for (index = 0x170; index <= 0x170; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}

	/* decode mode (0x17C) */
	/* debug       (0x180) */
	for (index = 0x17C; index <= 0x180; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}

	/* RDMA addr   (0x200) */
	for (index = 0x200; index <= 0x200; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}

	/* total MCU   (0x210) */
	for (index = 0x210; index <= 0x210; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
	/* file BRP addr   (0x248) */
	/* file size       (0x24C) */
	for (index = 0x248; index <= 0x24C; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
	/* IRQ              (0x274) */
	/* IRQ FSM          (0x278) */
	for (index = 0x274; index <= 0x278; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
	/* MCU CNT          (0x294) */
	for (index = 0x294; index <= 0x294; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_WRN("@0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
}

void jpeg_drv_dec_dump_reg(void)
{
	unsigned int reg_value = 0;
	unsigned int index = 0;

	JPEG_VEB("<<<<<= JPEG DEC DUMP =>>>>>\n");
	for (index = 0x8C; index <= 0x3FC; index += 4) {
		IMG_REG_READ(reg_value, JPEG_DEC_BASE + index);	/* reg_value = ioread32(JPEG_DEC_BASE + index); */
		JPEG_VEB("+0x%x(%d) 0x%08x\n", index, index / 4, reg_value);
		wait_pr();
	}
}


void jpeg_drv_dec_rw_reg(void)
{
	kal_uint32 i;
	unsigned long addr = 0;
	unsigned int reg_value = 0;
	/* kal_uint32 restore = 0; */


	JPEG_VEB("=======================================\n\r");
	JPEG_VEB("   JPEG decoder register RW test!!!!\n\r");

	/* for (i = 0x8C; i < 0x3FC; i+=4) */
	for (i = 0x090; i <= 0x294; i += 4) {
		addr = (unsigned long)(JPEG_DEC_BASE + i);
		JPEG_VEB("addr %03x(%03d) ", i, i / 4);

		IMG_REG_WRITE(0x00000000, addr);
		IMG_REG_READ(reg_value, addr);
		JPEG_VEB("write 0x00000000 read: 0x%08x\n", reg_value);

		IMG_REG_WRITE(0xffffffff, addr);
		IMG_REG_READ(reg_value, addr);
		JPEG_VEB("              write 0xffffffff read: 0x%08x\n", reg_value);

		wait_pr();
	}

	JPEG_VEB("=======================================\n\r\n\r");
}

#endif
