/******************************************************************************
Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <stdio.h>
#include <util/dstr.h>
#include <util/darray.h>
#include <util/platform.h>
#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif//__cplusplus

#include <codec_api.h>
//#include <measure_time.h>


#ifdef __cplusplus
}
#endif

#ifndef _STDINT_H_INCLUDED
#define _STDINT_H_INCLUDED
#endif



#define do_log(level, format, ...) blog(level, "[h264 encoder: '%s'] " format, obs_encoder_get_name(obsx264->encoder), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)
#ifndef WELS_ROUND
#define WELS_ROUND(x) ((int32_t)(0.5+(x)))
#endif//WELS_ROUND



typedef struct h264_param_t
{
	/* Video Properties */
	int         i_width;
	int         i_height;
	int         i_csp;         /* CSP of encoded bitstream */
	int         i_level_idc;
	int         i_frame_total; /* number of frames to encode if known, else 0 */

	/* Bitstream parameters */
	int	    i_bitrate;
	int         i_frame_reference;  /* Maximum number of reference frames */
	int         i_dpb_size;         /* Force a DPB size larger than that implied by B-frames and reference frames.
						* Useful in combination with interactive error resilience. */
	int         i_keyint_max;       /* Force an IDR keyframe at this interval */
	int         i_keyint_min;       /* Scenecuts closer together than this are coded as I, not IDR. */
	int         i_scenecut_threshold; /* how aggressively to insert extra I frames */
	int         b_intra_refresh;    /* Whether or not to use periodic intra refresh instead of IDR frames. */

	int         i_bframe;   /* how many b-frame between 2 references pictures */
	int         i_bframe_adaptive;
	int         i_bframe_bias;
	int         i_bframe_pyramid;   /* Keep some B-frames as references: 0=off, 1=strict hierarchical, 2=normal */
	int         b_open_gop;
	int         b_bluray_compat;
	int         i_avcintra_class;
	int	i_fps_num;
	int	i_fps_den;

	void(*param_free)(void*);


} h264_param_t;


struct obs_h264 {
	
	obs_encoder_t          *encoder;
	h264_param_t           params;
	ISVCEncoder                 *context;
	DARRAY(uint8_t)        packet_data;
	uint8_t                *extra_data;
	uint8_t                *sei;
	int32_t iFrameIdx;
	size_t                 extra_data_size;
	size_t                 sei_size;
	os_performance_token_t *performance_token;
	SFrameBSInfo sFbi;
	SEncParamExt sSvcParam;
	SEncParamBase sSvcParamBase;
	SSourcePicture sPic;
	FILE* pFpBs;
};

static int FillSpecificParameters(SEncParamExt* sParam, const h264_param_t* par);

/* ------------------------------------------------------------------------- */

static const char *obs_h264_getname(void *unused) {
	UNUSED_PARAMETER(unused);
	return "h264";
}

static void obs_h264_stop(void *data) {

};

static void clear_data(struct obs_h264 *obsx264) {
	if (obsx264->context) {
		WelsDestroySVCEncoder(obsx264->context);
		//fclose(obsx264->pFpBs);
		bfree(obsx264->sei);
		bfree(obsx264->extra_data);

		obsx264->context = NULL;
		obsx264->sei = NULL;
		obsx264->extra_data = NULL;
		obsx264->pFpBs = NULL;
	}
}

static void obs_h264_destroy(void *data) {
	struct obs_h264 *obsx264 = data;
	if (obsx264) {
		os_end_high_performance(obsx264->performance_token);
		clear_data(obsx264);
		da_free(obsx264->packet_data);
		bfree(obsx264);
	}
}

static void obs_h264_defaults(obs_data_t *settings) {
	obs_data_set_default_int(settings, "bitrate", 60000);
	obs_data_set_default_bool(settings, "use_bufsize", false);
	obs_data_set_default_int(settings, "buffer_size", 2500);
	obs_data_set_default_int(settings, "keyint_sec", 0);
	obs_data_set_default_int(settings, "crf", 23);
	obs_data_set_default_string(settings, "rate_control", "CBR");
	obs_data_set_default_string(settings, "preset", "veryfast");
	obs_data_set_default_string(settings, "profile", "");
	obs_data_set_default_string(settings, "tune", "");
	obs_data_set_default_string(settings, "x264opts", "");
}

static inline void add_strings(obs_property_t *list, const char *const *strings) {
	while (*strings) {
		obs_property_list_add_string(list, *strings, *strings);
		strings++;
	}
}

#define TEXT_BITRATE    obs_module_text("Bitrate")
#define TEXT_FPS    obs_module_text("Fps")
#define TEXT_X264_OPTS  obs_module_text("EncoderOptions")



static obs_properties_t *obs_h264_props(void *unused) {
	UNUSED_PARAMETER(unused);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int(props, "bitrate", TEXT_BITRATE, 50, 10000000, 1);
	obs_properties_add_text(props, "x264opts", TEXT_X264_OPTS,
		OBS_TEXT_DEFAULT);
	return props;
}

static bool getparam(const char *param, char **name, const char **value) {
	const char *assign;

	if (!param || !*param || (*param == '='))
		return false;

	assign = strchr(param, '=');
	if (!assign || !*assign || !*(assign + 1))
		return false;

	*name = bstrdup_n(param, assign - param);
	*value = assign + 1;
	return true;
}

static const char *validate(struct obs_h264 *obsx264, const char *val, const char *name, const char *const *list) {
	if (!val || !*val)
		return val;

	while (*list) {
		if (strcmp(val, *list) == 0)
			return val;

		list++;
	}

	warn("Invalid %s: %s", name, val);
	return NULL;
}

static void override_base_param(struct obs_h264 *obsx264, const char *param, char **preset, char **profile, char **tune) {
	char       *name;
	const char *val;

	if (getparam(param, &name, &val)) {
		if (astrcmpi(name, "preset") == 0) {
			//const char *valid_name = validate(obsx264, val,
			//	"preset", x264_preset_names);
			//if (valid_name) {
			//	bfree(*preset);
			//	*preset = bstrdup(val);
			//}

		}
		else if (astrcmpi(name, "profile") == 0) {
			////const char *valid_name = validate(obsx264, val,
			//	"profile", x264_profile_names);
			//if (valid_name) {
			//	bfree(*profile);
			//	*profile = bstrdup(val);
			//}

		}
		else if (astrcmpi(name, "tune") == 0) {
			//const char *valid_name = validate(obsx264, val,
			//	"tune", x264_tune_names);
			//if (valid_name) {
			//	bfree(*tune);
			//	*tune = bstrdup(val);
			//}
		}

		bfree(name);
	}
}

static inline void override_base_params(struct obs_h264 *obsx264, char **params, char **preset, char **profile, char **tune) {
	while (*params)
		override_base_param(obsx264, *(params++), preset, profile, tune);
}


static inline void set_param(struct obs_h264 *obsx264, const char *param) {
	char       *name;
	const char *val;

	if (getparam(param, &name, &val)) {
		if (strcmp(name, "preset") != 0 &&
			strcmp(name, "profile") != 0 &&
			strcmp(name, "tune") != 0 &&
			strcmp(name, "fps") != 0 &&
			strcmp(name, "force-cfr") != 0 &&
			strcmp(name, "width") != 0 &&
			strcmp(name, "height") != 0 &&
			strcmp(name, "opencl") != 0) {
			//if (strcmp(name, OPENCL_ALIAS) == 0)
			//	strcpy(name, "opencl");
			//if (x264_param_parse(&obsx264->params, name, val) != 0)
			//	warn("h264 param: %s failed", param);
			printf("name");
		}

		if (strcmp(name, "fps") != 0) {

		}

		bfree(name);
	}
}

static inline void apply_h264_profile(struct obs_h264 *obsx264, const char *profile) {
	if (!obsx264->context && profile && *profile) {
		//int ret = x264_param_apply_profile(&obsx264->params, profile);
		//if (ret != 0)
		//	warn("Failed to set x264 profile '%s'", profile);
	}
}

static inline const char *validate_preset(struct obs_h264 *obsx264, const char *preset) {
	//const char *new_preset = validate(obsx264, preset, "preset",
	//	x264_preset_names);
	//return new_preset ? new_preset : "veryfast";
	return "";
}

static bool reset_x264_params(struct obs_h264 *obsx264, const char *preset, const char *tune) {
	int ret = 0;// x264_param_default_preset(&obsx264->params,
		//	validate_preset(obsx264, preset),
	//		validate(obsx264, tune, "tune", x264_tune_names));
	return ret == 0;
}

static void log_h264(void *param, int level, const char *format, va_list args)
{
	struct obs_h264 *obsx264 = param;
	char str[1024];

	vsnprintf(str, 1024, format, args);
	info("%s", str);

	UNUSED_PARAMETER(level);
}

static inline const char *get_h264_colorspace_name(enum video_colorspace cs)
{
	switch (cs) {
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_601:
		return "undef";
	case VIDEO_CS_709:;
	}

	return "bt709";
}

static inline int get_h264_cs_val(enum video_colorspace cs,
	const char *const names[])
{
	const char *name = get_h264_colorspace_name(cs);
	int idx = 0;
	do {
		if (strcmp(names[idx], name) == 0)
			return idx;
	} while (!!names[++idx]);

	return 0;
}

static void obs_h264_video_info(void *data, struct video_scale_info *info);

enum rate_control {
	RATE_CONTROL_CBR,
	RATE_CONTROL_VBR,
	RATE_CONTROL_ABR,
	RATE_CONTROL_CRF
};

static void update_params(struct obs_h264 *obsx264, obs_data_t *settings,
	char **params)
{
	video_t *video = obs_encoder_video(obsx264->encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	struct video_scale_info info;

	info.format = voi->format;
	info.colorspace = voi->colorspace;
	info.range = voi->range;

	obs_h264_video_info(obsx264, &info);



	int bitrate = (int)obs_data_get_int(settings, "bitrate");
	int width = (int)obs_encoder_get_width(obsx264->encoder);
	int height = (int)obs_encoder_get_height(obsx264->encoder);
	bool use_bufsize = obs_data_get_bool(settings, "use_bufsize");
	bool cbr_override = obs_data_get_bool(settings, "cbr");

	obsx264->params.i_bitrate = bitrate;
	obsx264->params.i_width = width;
	obsx264->params.i_height = height;
	obsx264->params.i_fps_num = voi->fps_num;
	obsx264->params.i_fps_den = voi->fps_den;
	//obsx264->params.pf_log               = log_h264;
	//obsx264->params.p_log_private        = obsx264;
	//obsx264->params.i_log_level          = H264_LOG_WARNING;

	//if (obs_data_has_user_value(settings, "bf"))
	//	obsx264->params.i_bframe = bf;


	//obsx264->params.vui.i_transfer =
	//	get_x264_cs_val(info.colorspace, x264_transfer_names);
	//obsx264->params.vui.i_colmatrix =
	//	get_x264_cs_val(info.colorspace, x264_colmatrix_names);
	//obsx264->params.vui.i_colorprim =
	//	get_x264_cs_val(info.colorspace, x264_colorprim_names);
	//obsx264->params.vui.b_fullrange =
	//	info.range == VIDEO_RANGE_FULL;

	/* use the new filler method for CBR to allow real-time adjusting of
		* the bitrate */
		/*
		if (rc == RATE_CONTROL_CBR || rc == RATE_CONTROL_ABR) {
			obsx264->params.rc.i_rc_method   = X264_RC_ABR;

			if (rc == RATE_CONTROL_CBR) {
	#if X264_BUILD >= 139
				obsx264->params.rc.b_filler = true;
	#else
				obsx264->params.i_nal_hrd = X264_NAL_HRD_CBR;
	#endif
			}
		} else {
			obsx264->params.rc.i_rc_method   = X264_RC_CRF;
		}

		obsx264->params.rc.f_rf_constant = (float)crf;


		if (info.format == VIDEO_FORMAT_NV12)
			obsx264->params.i_csp = X264_CSP_NV12;
		else if (info.format == VIDEO_FORMAT_I420)
			obsx264->params.i_csp = X264_CSP_I420;
		else if (info.format == VIDEO_FORMAT_I444)
			obsx264->params.i_csp = X264_CSP_I444;
		else
			obsx264->params.i_csp = X264_CSP_NV12;
			*/

	while (*params)
		set_param(obsx264, *(params++));


}

static bool update_settings(struct obs_h264 *obsx264, obs_data_t *settings)
{
	char *preset = bstrdup(obs_data_get_string(settings, "preset"));
	char *profile = bstrdup(obs_data_get_string(settings, "profile"));
	char *tune = bstrdup(obs_data_get_string(settings, "tune"));
	const char *opts = obs_data_get_string(settings, "x264opts");

	char **paramlist;
	bool success = true;

	paramlist = strlist_split(opts, ' ', false);

	blog(LOG_INFO, "---------------------------------");

	if (!obsx264->context) {
		override_base_params(obsx264, paramlist,
			&preset, &profile, &tune);

		if (preset  && *preset)  info("preset: %s", preset);
		if (profile && *profile) info("profile: %s", profile);
		if (tune    && *tune)    info("tune: %s", tune);

		success = reset_x264_params(obsx264, preset, tune);
	}

	if (success) {
		update_params(obsx264, settings, paramlist);
		if (opts && *opts)
			info("custom settings: %s", opts);

		//if (!obsx264->context)
		//	apply_x264_profile(obsx264, profile);
	}

	//obsx264->params.b_repeat_headers = false;

	strlist_free(paramlist);
	bfree(preset);
	bfree(profile);
	bfree(tune);

	return success;
}

static bool obs_h264_update(void *data, obs_data_t *settings)
{
	struct obs_h264 *obsx264 = data;
	bool success = update_settings(obsx264, settings);
	int ret;

	if (success) {
		ret = 0;// x264_encoder_reconfig(obsx264->context, &obsx264->params);
		if (ret != 0)
			warn("Failed to reconfigure: %d", ret);
		return ret == 0;
	}

	return false;
}

static void load_headers(struct obs_h264 *obsx264){
	int             nal_count = 0;
	DARRAY(uint8_t) header;
	DARRAY(uint8_t) sei;

	da_init(header);
	da_init(sei);

	obsx264->extra_data = header.array;
	obsx264->extra_data_size = header.num;
	obsx264->sei = sei.array;
	obsx264->sei_size = sei.num;
}



static void *obs_h264_create(obs_data_t *settings, obs_encoder_t *encoder){
	struct obs_h264 *obsx264 = bzalloc(sizeof(struct obs_h264));
	obsx264->encoder = encoder;
	//obsx264->pFpBs = fopen("d:\\abcdef.bin", "wb");
	if (update_settings(obsx264, settings)) {
		int iRet = WelsCreateSVCEncoder(&obsx264->context);
		if (iRet) {
			warn("WelsCreateSVCEncoder() failed!!");
		}
		if (obsx264->context == NULL) {
			warn("x264 failed to load");
		}else {
			memset(&obsx264->sFbi, 0, sizeof(SFrameBSInfo));
			(*obsx264->context)->GetDefaultParams(obsx264->context, &obsx264->sSvcParam);
			video_t *video = obs_encoder_video(obsx264->encoder);
			const struct video_output_info *voi = video_output_get_info(video);

			obsx264->sSvcParam.fMaxFrameRate = voi->fps_num;
			//obsx264->sSvcParam.iNumRefFrame = 1;
			obsx264->sSvcParam.iPicHeight = voi->height;
			obsx264->sSvcParam.iPicWidth = voi->width;
			obsx264->sSvcParam.iRCMode = RC_TIMESTAMP_MODE;// RC_QUALITY_MODE;
			obsx264->sSvcParam.iTargetBitrate = (int)obs_data_get_int(settings, "bitrate");
			obsx264->sSvcParam.iUsageType = SCREEN_CONTENT_REAL_TIME;
			obsx264->sSvcParam.uiIntraPeriod = voi->fps_num * 10;
			obsx264->sSvcParam.eSpsPpsIdStrategy = CONSTANT_ID;
			obsx264->sSvcParam.iComplexityMode = LOW_COMPLEXITY;
			obsx264->sSvcParam.iMultipleThreadIdc = 1;
			obsx264->sSvcParam.bEnableLongTermReference = false;
			obsx264->sSvcParam.sSpatialLayers[0].fFrameRate = voi->fps_num;
			obsx264->sSvcParam.sSpatialLayers[0].iVideoWidth = voi->width;
			obsx264->sSvcParam.sSpatialLayers[0].iVideoHeight = voi->height;
			obsx264->sSvcParam.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
			obsx264->sSvcParam.sSpatialLayers[0].uiLevelIdc = LEVEL_2_2;
			obsx264->sSvcParam.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;
			obsx264->sSvcParam.sSpatialLayers[0].sSliceArgument.uiSliceNum = 1;
			int a = (*obsx264->context)->InitializeExt(obsx264->context, &obsx264->sSvcParam);

			load_headers(obsx264);
		}
	}
	else {
		warn("bad settings specified");
	}

	if (!obsx264->context) {
		bfree(obsx264);
		return NULL;
	}

	obsx264->performance_token = os_request_high_performance("x264 encoding");
	return obsx264;
}

// data 表示参数 frame表示需要编码的原始数据。packet表示输出的编码码流。received_packet表示本帧数据是否有编码输出。
static bool obs_h264_encode(void *data, struct encoder_frame *frame, struct encoder_packet *packet, bool *received_packet) {
	struct obs_h264 *obsx264 = data;
	if (!frame || !packet || !received_packet)
		return false;
	*received_packet = false;
	video_t *video = obs_encoder_video(obsx264->encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	int lines = 4;
	if (MAX_AV_PLANES < lines)
		lines = MAX_AV_PLANES;
	for (int i = 0; i < lines; i++) {
		if (frame->linesize[i] > 0) {
			obsx264->sPic.pData[i] = frame->data[i];
			obsx264->sPic.iStride[i] = frame->linesize[i];
		}
	}

	obsx264->sPic.iColorFormat = videoFormatI420;
	obsx264->sPic.iPicHeight = voi->height;
	obsx264->sPic.iPicWidth = voi->width;
	obsx264->sPic.uiTimeStamp = WELS_ROUND(obsx264->iFrameIdx * voi->fps_num / 1000);
	int iEncFrames = (*obsx264->context)->EncodeFrame(obsx264->context, &obsx264->sPic, &obsx264->sFbi);
	//warn("%d EncodeFrame: in[%d] [%d] [%d]",(int)time(NULL),(int)frame->linesize[0],(int)iEncFrames,(int)obsx264->sFbi.iFrameSizeInBytes);
	++obsx264->iFrameIdx;
	if (videoFrameTypeSkip == obsx264->sFbi.eFrameType) {
		return true;
	}

	if (iEncFrames == cmResultSuccess) {
		*received_packet = true;
		int iLayer = 0;
		int iFrameSize = 0;
		while (iLayer < obsx264->sFbi.iLayerNum) {
			SLayerBSInfo* pLayerBsInfo = &obsx264->sFbi.sLayerInfo[iLayer];
			if (pLayerBsInfo != NULL) {
				int iLayerSize = 0;
				int iNalIdx = pLayerBsInfo->iNalCount - 1;
				do {
					iLayerSize += pLayerBsInfo->pNalLengthInByte[iNalIdx];
					--iNalIdx;
				} while (iNalIdx >= 0);

				if (pLayerBsInfo->uiLayerType == VIDEO_CODING_LAYER) {
					if (obsx264->sSvcParam.iSpatialLayerNum == 1) {
						da_resize(obsx264->packet_data, 0);
						da_push_back_array(obsx264->packet_data, pLayerBsInfo->pBsBuf, iLayerSize);

						packet->data = obsx264->packet_data.array;
						packet->size = obsx264->packet_data.num;
						packet->type = OBS_ENCODER_VIDEO;
						packet->pts = obsx264->sFbi.uiTimeStamp * voi->fps_num / 1000;// *voi->fps_num / 90000;
						packet->dts = packet->pts;// pBS->DecodeTimeStamp * fps_num / 90000;
						packet->keyframe = obsx264->sFbi.eFrameType == videoFrameTypeIDR;// pic_out->b_keyframe != 0;
						warn("%d h264码流长度:%d[%d] s[%d] d[%d]\r\n",
							(int)time(NULL), (int)packet->size,(int)obsx264->sFbi.eFrameType,
							(int)frame->pts, (int)packet->pts);
					//	fwrite(pLayerBsInfo->pBsBuf, 1, iLayerSize , obsx264->pFpBs); // write pure bit stream into file
					}
					else { //multi bs file write
					}
				}
				else if(pLayerBsInfo->uiLayerType == NON_VIDEO_CODING_LAYER) {
					DARRAY(uint8_t) header;
					da_init(header);
					da_push_back_array(header, pLayerBsInfo->pBsBuf, iLayerSize);
					obsx264->extra_data = header.array;
					obsx264->extra_data_size = header.num;
					//fwrite(pLayerBsInfo->pBsBuf, 1, iLayerSize, obsx264->pFpBs);
					warn("%d NON_VIDEO_CODING_LAYER %d",(int)(time(NULL)),(int)header.num);
				}
				iFrameSize += iLayerSize;
			}
			++iLayer;
		}
	}
	return true;
}

static bool obs_h264_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	struct obs_h264 *obsx264 = data;

	if (!obsx264->context)
		return false;

	*extra_data = obsx264->extra_data;
	*size = obsx264->extra_data_size;
	return true;
}

static bool obs_h264_sei(void *data, uint8_t **sei, size_t *size)
{
	struct obs_h264 *obsx264 = data;

	if (!obsx264->context)
		return false;

	*sei = obsx264->sei;
	*size = obsx264->sei_size;
	return true;
}

static inline bool valid_format(enum video_format format)
{
	return format == VIDEO_FORMAT_I420;
	/*||
		format == VIDEO_FORMAT_NV12 ||
		format == VIDEO_FORMAT_I444;*/
}

static void obs_h264_video_info(void *data, struct video_scale_info *info)
{
	struct obs_h264 *obsx264 = data;
	enum video_format pref_format;

	pref_format = obs_encoder_get_preferred_video_format(obsx264->encoder);

	if (!valid_format(pref_format)) {
		pref_format = valid_format(info->format) ?
			info->format : VIDEO_FORMAT_I420;
	}

	info->format = pref_format;
}


struct obs_encoder_info obs_h264_encoder = {
	.id = "ext_h264",
	.type = OBS_ENCODER_VIDEO,
	.codec = "h264",
	.get_name = obs_h264_getname,
	.create = obs_h264_create,
	.destroy = obs_h264_destroy,
	.encode = obs_h264_encode,
	.update = obs_h264_update,
	.get_properties = obs_h264_props,
	.get_defaults = obs_h264_defaults,
	.get_extra_data = obs_h264_extra_data,
	.get_sei_data = obs_h264_sei,
	.get_video_info = obs_h264_video_info
};


static int FillSpecificParameters(SEncParamExt* sParam, const h264_param_t* par) {
	/* Test for temporal, spatial, SNR scalability */
	sParam->iUsageType = SCREEN_CONTENT_REAL_TIME;
	sParam->fMaxFrameRate = 5;                // input frame rate
	sParam->iPicWidth = par->i_width;                 // width of picture in samples
	sParam->iPicHeight = par->i_height;                  // height of picture in samples
	sParam->iTargetBitrate = par->i_bitrate;              // target bitrate desired
	sParam->iMaxBitrate = par->i_bitrate * 130 / 100;
	sParam->iRCMode = RC_QUALITY_MODE;      //  rc mode control
	sParam->iTemporalLayerNum = 3;    // layer number at temporal level
	sParam->iSpatialLayerNum = 1;    // layer number at spatial level
	sParam->bEnableDenoise = 0;    // denoise control
	sParam->bEnableBackgroundDetection = 1; // background detection control
	sParam->bEnableAdaptiveQuant = 1; // adaptive quantization control
	sParam->bEnableFrameSkip = 1; // frame skipping
	sParam->bEnableLongTermReference = 0; // long term reference control
	sParam->iLtrMarkPeriod = 30;
	sParam->uiIntraPeriod = 320;           // period of Intra frame
	sParam->eSpsPpsIdStrategy = CONSTANT_ID;
	sParam->bPrefixNalAddingCtrl = 0;
	sParam->iComplexityMode = LOW_COMPLEXITY;
	sParam->bSimulcastAVC = false;
	int iIndexLayer = 0;
	sParam->sSpatialLayers[iIndexLayer].uiProfileIdc = PRO_BASELINE;
	sParam->sSpatialLayers[iIndexLayer].iVideoWidth = sParam->iPicWidth;
	sParam->sSpatialLayers[iIndexLayer].iVideoHeight = sParam->iPicHeight;
	sParam->sSpatialLayers[iIndexLayer].fFrameRate = sParam->fMaxFrameRate;
	sParam->sSpatialLayers[iIndexLayer].iSpatialBitrate = sParam->iTargetBitrate;
	sParam->sSpatialLayers[iIndexLayer].iMaxSpatialBitrate = sParam->iMaxBitrate;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_FIXEDSLCNUM_SLICE;

	++iIndexLayer;
	sParam->sSpatialLayers[iIndexLayer].uiProfileIdc = PRO_SCALABLE_BASELINE;
	sParam->sSpatialLayers[iIndexLayer].iVideoWidth = 320;
	sParam->sSpatialLayers[iIndexLayer].iVideoHeight = 180;
	sParam->sSpatialLayers[iIndexLayer].fFrameRate = 15.0f;
	sParam->sSpatialLayers[iIndexLayer].iSpatialBitrate = 160000;
	sParam->sSpatialLayers[iIndexLayer].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

	++iIndexLayer;
	sParam->sSpatialLayers[iIndexLayer].uiProfileIdc = PRO_SCALABLE_BASELINE;
	sParam->sSpatialLayers[iIndexLayer].iVideoWidth = 640;
	sParam->sSpatialLayers[iIndexLayer].iVideoHeight = 360;
	sParam->sSpatialLayers[iIndexLayer].fFrameRate = 30.0f;
	sParam->sSpatialLayers[iIndexLayer].iSpatialBitrate = 512000;
	sParam->sSpatialLayers[iIndexLayer].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceNum = 1;

	++iIndexLayer;
	sParam->sSpatialLayers[iIndexLayer].uiProfileIdc = PRO_SCALABLE_BASELINE;
	sParam->sSpatialLayers[iIndexLayer].iVideoWidth = 1280;
	sParam->sSpatialLayers[iIndexLayer].iVideoHeight = 720;
	sParam->sSpatialLayers[iIndexLayer].fFrameRate = 30.0f;
	sParam->sSpatialLayers[iIndexLayer].iSpatialBitrate = 1500000;
	sParam->sSpatialLayers[iIndexLayer].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceNum = 1;

	float fMaxFr = sParam->sSpatialLayers[sParam->iSpatialLayerNum - 1].fFrameRate;
	for (int32_t i = sParam->iSpatialLayerNum - 2; i >= 0; --i) {
		if (sParam->sSpatialLayers[i].fFrameRate > fMaxFr + 1)
			fMaxFr = sParam->sSpatialLayers[i].fFrameRate;
	}
	sParam->fMaxFrameRate = fMaxFr;
	return 0;
}
