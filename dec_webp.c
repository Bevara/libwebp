/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / WebP decoder filter, based on libwebp
 *  (https://chromium.googlesource.com/webm/libwebp). Only the still-image
 *  decoder is linked (libwebpdecoder.a); animated WebP would additionally
 *  need libwebpdemux and a frame loop, and is not handled here - an animated
 *  file decodes to its first frame.
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <webp/decode.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_WEBPDecCtx;

static GF_Err webpdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_WEBPDecCtx *ctx = (GF_WEBPDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	/* Must be on the PID before any data flows, see the comment in dec_qoi.c:
	 * GPAC resolves the output link from this property alone. */
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool webpdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_WEBPDecCtx *ctx = (GF_WEBPDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err webpdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size;
	int width = 0, height = 0;
	uint8_t *pixels;
	WebPBitstreamFeatures features;
	Bool has_alpha;
	GF_WEBPDecCtx *ctx = (GF_WEBPDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	/* The features tell us whether the file carries alpha, so RGB files do
	 * not get a fabricated opaque alpha plane. */
	if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[WEBPDec] Not a valid WebP bitstream\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	has_alpha = features.has_alpha ? GF_TRUE : GF_FALSE;

	if (has_alpha)
		pixels = WebPDecodeRGBA(data, size, &width, &height);
	else
		pixels = WebPDecodeRGB(data, size, &width, &height);
	gf_filter_pid_drop_packet(ctx->ipid);

	if (!pixels)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[WEBPDec] Failed to decode WebP image\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	out_size = (u32)width * (u32)height * (has_alpha ? 4 : 3);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT((u32)width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT((u32)height));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT((u32)width * (has_alpha ? 4 : 3)));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(has_alpha ? GF_PIXEL_RGBA : GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		WebPFree(pixels);
		return GF_OUT_OF_MEM;
	}
	memcpy(output, pixels, out_size);
	WebPFree(pixels);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void webpdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability WEBPDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "webp"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/webp"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister WEBPDecoderRegister = {
	.name = "webpdec",
	GF_FS_SET_DESCRIPTION("WebP image decoder")
		GF_FS_SET_HELP("This filter decodes WebP images (lossy, lossless and alpha) using libwebp.")
			.private_size = sizeof(GF_WEBPDecCtx),
	SETCAPS(WEBPDecCaps),
	.configure_pid = webpdec_configure_pid,
	.process = webpdec_process,
	.process_event = webpdec_process_event,
	.finalize = webpdec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE webpdec_register(GF_FilterSession *session)
{
	return &WEBPDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_webpdec(void) {
    gf_filter_auto_register("webpdec", webpdec_register);
}
