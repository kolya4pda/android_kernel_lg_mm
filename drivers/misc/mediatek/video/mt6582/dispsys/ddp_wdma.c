#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <generated/autoconf.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/param.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/aee.h>

#include <linux/xlog.h>

#include <asm/io.h>
#include <asm/system.h>

#include <mach/mt_typedefs.h>

#include "ddp_matrix_para.h"
#include "ddp_reg.h"
#include "ddp_wdma.h"
#include "ddp_hal.h"
#include "ddp_drv.h"
#include "ddp_debug.h"
#include "smi_common.h"

#define DISP_INDEX_OFFSET 0
#define ALIGN_TO(x, n)  \
	(((x) + ((n) - 1)) & ~((n) - 1))

enum WDMA_OUTPUT_FORMAT {
	WDMA_OUTPUT_FORMAT_RGB565 = 0x00,	/* basic format */
	WDMA_OUTPUT_FORMAT_RGB888 = 0x01,
	WDMA_OUTPUT_FORMAT_ARGB = 0x02,
	WDMA_OUTPUT_FORMAT_XRGB = 0x03,
	WDMA_OUTPUT_FORMAT_UYVY = 0x04,
	WDMA_OUTPUT_FORMAT_GREY = 0x05,
	WDMA_OUTPUT_FORMAT_YV12 = 0x06,
	WDMA_OUTPUT_FORMAT_NV12 = 0x07,
	WDMA_OUTPUT_FORMAT_NV21 = 0x08,

	WDMA_OUTPUT_FORMAT_BGR888 = 0xa0,	/* special format by swap */
	WDMA_OUTPUT_FORMAT_BGRA = 0xa1,
	WDMA_OUTPUT_FORMAT_ABGR = 0xa2,
	WDMA_OUTPUT_FORMAT_RGBA = 0xa3,
	WDMA_OUTPUT_FORMAT_YUYV = 0xa4,
	WDMA_OUTPUT_FORMAT_I420 = 0xa5,
	WDMA_OUTPUT_FORMAT_XBGR = 0xa6,

	WDMA_OUTPUT_FORMAT_UNKNOWN = 0x100
	    /*WDMA_OUTPUT_FORMAT_Y1V0Y0U0,   // UV format by swap
	       WDMA_OUTPUT_FORMAT_V0Y1U0Y0,
	       WDMA_OUTPUT_FORMAT_Y1U0Y0V0,
	       WDMA_OUTPUT_FORMAT_U0Y1V0Y0,// */
};

enum WDMA_COLOR_SPACE {
	WDMA_COLOR_SPACE_RGB = 0,
	WDMA_COLOR_SPACE_YUV,
};

static int WDMADumpHidenReg(unsigned idx)
{
	unsigned int i = 0;
	DISP_MSG("[DDP] dump WDMA 0xac:\n [DDP]:");
	for (i = 0; i < 16; i++) {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_REG_MASK,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, i);
		DISP_MSG("0x%x, ", DISP_REG_GET(DISP_REG_WDMA_CFG));
	}
	return 0;
}

int WDMAStart(unsigned idx)
{


	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_INTEN, 0x03);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_EN, 0x01);

	return 0;
}

int WDMAStop(unsigned idx)
{


	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_INTEN, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_EN, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_INTSTA, 0x00);

	return 0;
}

int WDMAReset(unsigned idx)
{
	unsigned int delay_cnt = 0;
	static unsigned int wdma_timeout_cnt;

	/* WDMA_RST = 0x00; */
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_RST, 0x01);	/* soft reset */
	while ((DISP_REG_GET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_FLOW_CTRL_DBG) & 0x1) != 0x1) {
		delay_cnt++;
		if (delay_cnt > 10000) {
			DISP_ERR("[DDP] error, WDMAReset(%d) timeout! wdma_timeout_cnt=%d\n", idx,
				 wdma_timeout_cnt++);
			WDMADumpHidenReg(idx);
			disp_dump_reg(DISP_MODULE_WDMA0);
			smi_dumpDebugMsg();
			break;
		}
	}
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_RST, 0x00);

	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SRC_SIZE, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_SIZE, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_COORD, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_ADDR, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_W_IN_BYTE, 0x00);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_ALPHA, 0x00);	/* clear regs */

	return 0;
}

int WDMAConfigUV(unsigned idx, unsigned int uAddr, unsigned int vAddr, unsigned int dstWidth)
{
	unsigned int bpp = 1;

	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_U_ADDR, uAddr);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_V_ADDR, vAddr);
	DISP_REG_SET_FIELD(WDMA_BUF_ADDR_FLD_UV_Pitch,
			   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_UV_PITCH,
			   dstWidth * bpp / 2);

	return 0;
}

int WDMAConfig(unsigned idx,
	       unsigned inputFormat,
	       unsigned srcWidth,
	       unsigned srcHeight,
	       unsigned clipX,
	       unsigned clipY,
	       unsigned clipWidth,
	       unsigned clipHeight,
	       DpColorFormat out_formt,
	       unsigned dstAddress, unsigned dstWidth, bool useSpecifiedAlpha, unsigned char alpha)
{
	unsigned int output_format = 0;
	unsigned int byte_swap = 0;
	unsigned int rgb_swap = 0;
	unsigned int uv_swap = 0;

	unsigned input_color_space;	/* check input format color space */
	unsigned output_color_space;	/* check output format color space */
	unsigned mode = 0xdeaddead;
	unsigned bpp;
	unsigned pitch, pitchUV;
	unsigned char *yAddr, *uAddr, *vAddr;
	enum WDMA_OUTPUT_FORMAT outputFormat = wdma_fmt_convert(out_formt);

	ASSERT((WDMA_INPUT_FORMAT_ARGB == inputFormat) ||
	       (WDMA_INPUT_FORMAT_YUV444 == inputFormat));

	/* should use OVL alpha instead of sw config */
	useSpecifiedAlpha = 0;
	alpha = 0xff;

	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SRC_SIZE, srcHeight << 16 | srcWidth);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_COORD, clipY << 16 | clipX);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_SIZE,
		     clipHeight << 16 | clipWidth);

	DISP_REG_SET_FIELD(WDMA_CFG_FLD_In_Format, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   inputFormat);


	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_RGB565:
	case WDMA_OUTPUT_FORMAT_RGB888:
	case WDMA_OUTPUT_FORMAT_ARGB:
	case WDMA_OUTPUT_FORMAT_XRGB:
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_GREY:
	case WDMA_OUTPUT_FORMAT_YV12:
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		output_format = outputFormat;
		byte_swap = 0;
		rgb_swap = 0;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_YUYV:
		output_format = WDMA_OUTPUT_FORMAT_UYVY;
		byte_swap = 0;
		rgb_swap = 0;
		uv_swap = 1;
		break;
	case WDMA_OUTPUT_FORMAT_I420:
		output_format = WDMA_OUTPUT_FORMAT_YV12;
		byte_swap = 0;
		rgb_swap = 0;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_BGR888:
		output_format = WDMA_OUTPUT_FORMAT_RGB888;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_BGRA:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 1;
		rgb_swap = 0;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_ABGR:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_RGBA:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 1;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_XBGR:
		output_format = WDMA_OUTPUT_FORMAT_XRGB;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	default:
		ASSERT(0);	/* invalid format */
	}
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_Out_Format, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   output_format);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_BYTE_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   byte_swap);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_RGB_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   rgb_swap);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_UV_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   uv_swap);


	/* set DNSP for UYVY and YUV_3P format for better quality */
	if (outputFormat == WDMA_OUTPUT_FORMAT_UYVY || outputFormat == WDMA_OUTPUT_FORMAT_YUYV) {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_DNSP_SEL,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 1);
	} else {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_DNSP_SEL,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 0);
	}

	switch (inputFormat) {
	case WDMA_INPUT_FORMAT_ARGB:
		input_color_space = WDMA_COLOR_SPACE_RGB;
		break;
	case WDMA_INPUT_FORMAT_YUV444:
		input_color_space = WDMA_COLOR_SPACE_YUV;
		break;
	default:
		ASSERT(0);
	}

	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_RGB565:
	case WDMA_OUTPUT_FORMAT_RGB888:
	case WDMA_OUTPUT_FORMAT_ARGB:
	case WDMA_OUTPUT_FORMAT_XRGB:
	case WDMA_OUTPUT_FORMAT_BGR888:
	case WDMA_OUTPUT_FORMAT_BGRA:
	case WDMA_OUTPUT_FORMAT_ABGR:
	case WDMA_OUTPUT_FORMAT_RGBA:
	case WDMA_OUTPUT_FORMAT_XBGR:
		output_color_space = WDMA_COLOR_SPACE_RGB;
		break;
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_GREY:
	case WDMA_OUTPUT_FORMAT_YV12:
	case WDMA_OUTPUT_FORMAT_I420:
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		output_color_space = WDMA_COLOR_SPACE_YUV;
		break;
	default:
		ASSERT(0);
	}

	if (WDMA_COLOR_SPACE_RGB == input_color_space && WDMA_COLOR_SPACE_YUV == output_color_space) {
		/* RGB to YUV required */
		mode = RGB2YUV_601;
	} else if (WDMA_COLOR_SPACE_YUV == input_color_space &&
		   WDMA_COLOR_SPACE_RGB == output_color_space) {
		/* YUV to RGB required */
		mode = YUV2RGB_601_16_16;
	}

	if (TABLE_NO > mode) {	/* set matrix as mode */
		DISP_REG_SET_FIELD(WDMA_C00_FLD_C00, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C00,
				   coef[mode][0][0]);
		DISP_REG_SET_FIELD(WDMA_C00_FLD_C01, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C00,
				   coef[mode][0][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C02, coef[mode][0][2]);

		DISP_REG_SET_FIELD(WDMA_C10_FLD_C10, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C10,
				   coef[mode][1][0]);
		DISP_REG_SET_FIELD(WDMA_C10_FLD_C11, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C10,
				   coef[mode][1][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C12, coef[mode][1][2]);

		DISP_REG_SET_FIELD(WDMA_C20_FLD_C20, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C20,
				   coef[mode][2][0]);
		DISP_REG_SET_FIELD(WDMA_C20_FLD_C21, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C20,
				   coef[mode][2][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C22, coef[mode][2][2]);

		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_PRE_ADD_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0,
				   coef[mode][3][0]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_SIGNED_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0, 0);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_PRE_ADD_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0,
				   coef[mode][3][1]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_SIGNED_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0, 0);

		DISP_REG_SET_FIELD(WDMA_PRE_ADD2_FLD_PRE_ADD_2,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD2,
				   coef[mode][3][2]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD2_FLD_SIGNED_2,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD2, 0);

		DISP_REG_SET_FIELD(WDMA_POST_ADD0_FLD_POST_ADD_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD0,
				   coef[mode][4][0]);
		DISP_REG_SET_FIELD(WDMA_POST_ADD0_FLD_POST_ADD_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD0,
				   coef[mode][4][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD2, coef[mode][4][2]);

		DISP_REG_SET_FIELD(WDMA_CFG_FLD_EXT_MTX_EN,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 1);
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_CT_EN, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
				   1);
	} else {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_EXT_MTX_EN,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 0);
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_CT_EN, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
				   0);
	}
	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_RGB565:
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_YUYV:
		bpp = 2;
		break;
	case WDMA_OUTPUT_FORMAT_GREY:
	case WDMA_OUTPUT_FORMAT_YV12:
	case WDMA_OUTPUT_FORMAT_I420:
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		bpp = 1;
		break;
	case WDMA_OUTPUT_FORMAT_RGB888:
	case WDMA_OUTPUT_FORMAT_BGR888:
		bpp = 3;
		break;
	case WDMA_OUTPUT_FORMAT_ARGB:
	case WDMA_OUTPUT_FORMAT_XRGB:
	case WDMA_OUTPUT_FORMAT_BGRA:
	case WDMA_OUTPUT_FORMAT_ABGR:
	case WDMA_OUTPUT_FORMAT_RGBA:
	case WDMA_OUTPUT_FORMAT_XBGR:
		bpp = 4;
		break;
	default:
		ASSERT(0);	/* invalid format */
	}
	pitch = srcWidth * bpp;
	/* YUV format parameters */
	yAddr = (unsigned char *)dstAddress;
	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_YUYV:
	case WDMA_OUTPUT_FORMAT_GREY:
		uAddr = yAddr;
		vAddr = yAddr;
		break;
	case WDMA_OUTPUT_FORMAT_YV12:
		pitch = ALIGN_TO(pitch, 16);
		pitchUV = ALIGN_TO(pitch / 2, 16);
		vAddr = yAddr + pitch * srcHeight;
		uAddr = vAddr + pitchUV * srcHeight / 2;
		break;
	case WDMA_OUTPUT_FORMAT_I420:
		pitchUV = pitch / 2;
		uAddr = yAddr + pitch * srcHeight;
		vAddr = uAddr + pitchUV * srcHeight / 2;
		break;
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		pitchUV = pitch / 2;
		uAddr = yAddr + pitch * srcHeight;
		vAddr = uAddr;
		break;
	default:
		break;
	}
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_ADDR, dstAddress);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_W_IN_BYTE, pitch);

	if (WDMA_COLOR_SPACE_YUV == output_color_space) {
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_U_ADDR,
			     (unsigned int)uAddr);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_V_ADDR,
			     (unsigned int)vAddr);
		DISP_REG_SET_FIELD(WDMA_BUF_ADDR_FLD_UV_Pitch,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_UV_PITCH, pitchUV);
	}
	DISP_REG_SET_FIELD(WDMA_ALPHA_FLD_A_Sel, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_ALPHA,
			   useSpecifiedAlpha);
	DISP_REG_SET_FIELD(WDMA_ALPHA_FLD_A_Value, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_ALPHA,
			   alpha);

	return 0;
}

int WDMAConfig2(unsigned idx,
		unsigned inputFormat,
		unsigned srcWidth,
		unsigned srcHeight,
		unsigned clipX,
		unsigned clipY,
		unsigned clipWidth,
		unsigned clipHeight,
		DpColorFormat out_formt,
		unsigned dstAddress,
		unsigned dstWidth,
		unsigned dstPitch, unsigned dstUVPitch, bool useSpecifiedAlpha, unsigned char alpha)
{
	unsigned int output_format = 0;
	unsigned int byte_swap = 0;
	unsigned int rgb_swap = 0;
	unsigned int uv_swap = 0;

	unsigned input_color_space;	/* check input format color space */
	unsigned output_color_space;	/* check output format color space */
	unsigned mode = 0xdeaddead;
	/* unsigned bpp; */
	unsigned char *yAddr, *uAddr, *vAddr;
	enum WDMA_OUTPUT_FORMAT outputFormat = wdma_fmt_convert(out_formt);

	ASSERT((WDMA_INPUT_FORMAT_ARGB == inputFormat) ||
	       (WDMA_INPUT_FORMAT_YUV444 == inputFormat));

	/* should use OVL alpha instead of sw config */
	useSpecifiedAlpha = 0;
	alpha = 0xff;

	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SRC_SIZE, srcHeight << 16 | srcWidth);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_COORD, clipY << 16 | clipX);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CLIP_SIZE,
		     clipHeight << 16 | clipWidth);

	DISP_REG_SET_FIELD(WDMA_CFG_FLD_In_Format, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   inputFormat);

	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_RGB565:
	case WDMA_OUTPUT_FORMAT_RGB888:
	case WDMA_OUTPUT_FORMAT_ARGB:
	case WDMA_OUTPUT_FORMAT_XRGB:
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_GREY:
	case WDMA_OUTPUT_FORMAT_YV12:
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		output_format = outputFormat;
		byte_swap = 0;
		rgb_swap = 0;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_YUYV:
		output_format = WDMA_OUTPUT_FORMAT_UYVY;
		byte_swap = 0;
		rgb_swap = 0;
		uv_swap = 1;
		break;
	case WDMA_OUTPUT_FORMAT_BGR888:
		output_format = WDMA_OUTPUT_FORMAT_RGB888;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_BGRA:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 1;
		rgb_swap = 0;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_ABGR:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_RGBA:
		output_format = WDMA_OUTPUT_FORMAT_ARGB;
		byte_swap = 1;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	case WDMA_OUTPUT_FORMAT_XBGR:
		output_format = WDMA_OUTPUT_FORMAT_XRGB;
		byte_swap = 0;
		rgb_swap = 1;
		uv_swap = 0;
		break;
	default:
		DISP_ERR("[DDP] error: invalid color format:0x%x\n", outputFormat);
		ASSERT(0);	/* invalid format */
	}
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_Out_Format, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   output_format);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_BYTE_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   byte_swap);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_RGB_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   rgb_swap);
	DISP_REG_SET_FIELD(WDMA_CFG_FLD_UV_SWAP, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
			   uv_swap);


	/* set DNSP for UYVY and YUV_3P format for better quality */
	if (outputFormat == WDMA_OUTPUT_FORMAT_UYVY || outputFormat == WDMA_OUTPUT_FORMAT_YUYV) {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_DNSP_SEL,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 1);
	} else {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_DNSP_SEL,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 0);
	}

	switch (inputFormat) {
	case WDMA_INPUT_FORMAT_ARGB:
		input_color_space = WDMA_COLOR_SPACE_RGB;
		break;
	case WDMA_INPUT_FORMAT_YUV444:
		input_color_space = WDMA_COLOR_SPACE_YUV;
		break;
	default:
		DISP_ERR("[DDP] error: invalid color format:0x%x\n", inputFormat);
		ASSERT(0);
	}

	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_RGB565:
	case WDMA_OUTPUT_FORMAT_RGB888:
	case WDMA_OUTPUT_FORMAT_ARGB:
	case WDMA_OUTPUT_FORMAT_XRGB:
	case WDMA_OUTPUT_FORMAT_BGR888:
	case WDMA_OUTPUT_FORMAT_BGRA:
	case WDMA_OUTPUT_FORMAT_ABGR:
	case WDMA_OUTPUT_FORMAT_RGBA:
	case WDMA_OUTPUT_FORMAT_XBGR:
		output_color_space = WDMA_COLOR_SPACE_RGB;
		break;
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_YUYV:
	case WDMA_OUTPUT_FORMAT_GREY:
	case WDMA_OUTPUT_FORMAT_YV12:
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		output_color_space = WDMA_COLOR_SPACE_YUV;
		break;
	default:
		DISP_ERR("[DDP] error: invalid color format:0x%x\n", outputFormat);
		ASSERT(0);
	}

	if (WDMA_COLOR_SPACE_RGB == input_color_space && WDMA_COLOR_SPACE_YUV == output_color_space) {
		/* RGB to YUV required */
		mode = RGB2YUV_601;
	} else if (WDMA_COLOR_SPACE_YUV == input_color_space &&
		   WDMA_COLOR_SPACE_RGB == output_color_space) {
		/* YUV to RGB required */
		mode = YUV2RGB_601_16_16;
	}

	if (TABLE_NO > mode) {	/* set matrix as mode */
		DISP_REG_SET_FIELD(WDMA_C00_FLD_C00, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C00,
				   coef[mode][0][0]);
		DISP_REG_SET_FIELD(WDMA_C00_FLD_C01, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C00,
				   coef[mode][0][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C02, coef[mode][0][2]);

		DISP_REG_SET_FIELD(WDMA_C10_FLD_C10, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C10,
				   coef[mode][1][0]);
		DISP_REG_SET_FIELD(WDMA_C10_FLD_C11, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C10,
				   coef[mode][1][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C12, coef[mode][1][2]);

		DISP_REG_SET_FIELD(WDMA_C20_FLD_C20, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C20,
				   coef[mode][2][0]);
		DISP_REG_SET_FIELD(WDMA_C20_FLD_C21, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C20,
				   coef[mode][2][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_C22, coef[mode][2][2]);

		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_PRE_ADD_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0,
				   coef[mode][3][0]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_SIGNED_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0, 0);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_PRE_ADD_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0,
				   coef[mode][3][1]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD0_FLD_SIGNED_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD0, 0);

		DISP_REG_SET_FIELD(WDMA_PRE_ADD2_FLD_PRE_ADD_2,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD2,
				   coef[mode][3][2]);
		DISP_REG_SET_FIELD(WDMA_PRE_ADD2_FLD_SIGNED_2,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_PRE_ADD2, 0);

		DISP_REG_SET_FIELD(WDMA_POST_ADD0_FLD_POST_ADD_0,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD0,
				   coef[mode][4][0]);
		DISP_REG_SET_FIELD(WDMA_POST_ADD0_FLD_POST_ADD_1,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD0,
				   coef[mode][4][1]);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_POST_ADD2, coef[mode][4][2]);

		DISP_REG_SET_FIELD(WDMA_CFG_FLD_EXT_MTX_EN,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 1);
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_CT_EN, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
				   1);
	} else {
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_EXT_MTX_EN,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG, 0);
		DISP_REG_SET_FIELD(WDMA_CFG_FLD_CT_EN, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_CFG,
				   0);
	}
	/* YUV format parameters */
	yAddr = (unsigned char *)dstAddress;
	switch (outputFormat) {
	case WDMA_OUTPUT_FORMAT_UYVY:
	case WDMA_OUTPUT_FORMAT_YUYV:
	case WDMA_OUTPUT_FORMAT_GREY:
		uAddr = yAddr;
		vAddr = yAddr;
		break;
	case WDMA_OUTPUT_FORMAT_YV12:
		/* TODO: remove this input parameter, just ignore and recalculate it now. */
		dstUVPitch = ALIGN_TO(dstPitch / 2, 16);
		vAddr = yAddr + dstPitch * srcHeight;
		uAddr = vAddr + dstUVPitch * srcHeight / 2;
		break;
	case WDMA_OUTPUT_FORMAT_NV12:
	case WDMA_OUTPUT_FORMAT_NV21:
		uAddr = yAddr + dstPitch * srcHeight;
		vAddr = uAddr;
		break;
	default:
		break;
	}
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_ADDR, dstAddress);
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_W_IN_BYTE, dstPitch);
	if (WDMA_COLOR_SPACE_YUV == output_color_space) {
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_U_ADDR,
			     (unsigned int)uAddr);
		DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_V_ADDR,
			     (unsigned int)vAddr);
		DISP_REG_SET_FIELD(WDMA_BUF_ADDR_FLD_UV_Pitch,
				   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_UV_PITCH,
				   dstUVPitch);
	}
	DISP_REG_SET_FIELD(WDMA_ALPHA_FLD_A_Sel, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_ALPHA,
			   useSpecifiedAlpha);
	DISP_REG_SET_FIELD(WDMA_ALPHA_FLD_A_Value, idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_ALPHA,
			   alpha);

	return 0;
}

unsigned int WDMAGetDstAddress(unsigned int idx)
{
	return DISP_REG_GET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_ADDR);
}

void WDMAConfigAddress(unsigned int idx, unsigned int addr)
{
	DISP_REG_SET(idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_DST_ADDR, addr);
}

void WDMAWait(unsigned idx)
{
	/* polling interrupt status */
	unsigned int delay_cnt = 0;
	while ((DISP_REG_GET(0x1000 * idx + DISP_REG_WDMA_INTSTA) & 0x1) != 0x1) {
		delay_cnt++;
		/*msleep(1);*/
		/* use usleep_range(min, max) instead */
		usleep_range(1000, 1100);
		if (delay_cnt > 100) {
			DISP_ERR("[DDP] error:WDMA%dWait timeout\n", idx);
			break;
		}
	}
	DISP_REG_SET(0x1000 * idx + DISP_REG_WDMA_INTSTA, 0x0);

}

void WDMASlowMode(unsigned int idx,
		  unsigned int enable,
		  unsigned int level, unsigned int cnt, unsigned int threadhold)
{

	DISP_REG_SET_FIELD(WDMA_SMI_CON_FLD_Slow_Enable,
			   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SMI_CON, enable & 0x01);
	DISP_REG_SET_FIELD(WDMA_SMI_CON_FLD_Slow_Count,
			   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SMI_CON, cnt & 0xff);
	DISP_REG_SET_FIELD(WDMA_SMI_CON_FLD_Slow_Level,
			   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SMI_CON, level & 0x7);
	DISP_REG_SET_FIELD(WDMA_SMI_CON_FLD_Threshold,
			   idx * DISP_INDEX_OFFSET + DISP_REG_WDMA_SMI_CON, threadhold & 0xf);

}

enum WDMA_OUTPUT_FORMAT wdma_fmt_convert(DpColorFormat fmt)
{
	enum WDMA_OUTPUT_FORMAT wdma_fmt = WDMA_OUTPUT_FORMAT_UNKNOWN;
	switch (fmt) {
	case eRGB565:
		wdma_fmt = WDMA_OUTPUT_FORMAT_RGB565;
		break;
	case eRGB888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_RGB888;
		break;
	case eBGR888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_BGR888;
		break;
	case eARGB8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_ARGB;
		break;
	case eXARGB8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_XRGB;
		break;
	case eXABGR8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_XBGR;
		break;
	case eYUYV:
		wdma_fmt = WDMA_OUTPUT_FORMAT_YUYV;
		break;
	case eUYVY:
		wdma_fmt = WDMA_OUTPUT_FORMAT_UYVY;
		break;
	case eGREY:
		wdma_fmt = WDMA_OUTPUT_FORMAT_GREY;
		break;
	case eYV12:
		wdma_fmt = WDMA_OUTPUT_FORMAT_YV12;
		break;
	case eI420:
		wdma_fmt = WDMA_OUTPUT_FORMAT_I420;
		break;
	case eNV12:
		wdma_fmt = WDMA_OUTPUT_FORMAT_NV12;
		break;
	case eNV21:
		wdma_fmt = WDMA_OUTPUT_FORMAT_NV21;
		break;

	case eBGRA8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_BGRA;
		break;
	case eABGR8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_ABGR;
		break;
	case eRGBA8888:
		wdma_fmt = WDMA_OUTPUT_FORMAT_RGBA;
		break;
	default:
		DISP_ERR("DDP error, unknow wdma_fmt=%d\n", wdma_fmt);
	}

	return wdma_fmt;
}
