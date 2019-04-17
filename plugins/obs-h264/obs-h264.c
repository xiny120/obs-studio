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

#include <codec_api.h>

#ifndef _STDINT_H_INCLUDED
#define _STDINT_H_INCLUDED
#endif

#define do_log(level, format, ...) blog(level, "[h264 encoder: '%s'] " format, obs_encoder_get_name(obsx264->encoder), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO,    format, ##__VA_ARGS__)
#define debug(format, ...) do_log(LOG_DEBUG,   format, ##__VA_ARGS__)


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
	size_t                 extra_data_size;
	size_t                 sei_size;
	os_performance_token_t *performance_token;
	SFrameBSInfo sFbi;
	SEncParamExt sSvcParam;
};

/* ------------------------------------------------------------------------- */

static const char *obs_h264_getname(void *unused) {
	UNUSED_PARAMETER(unused);
	return "h264";
}

static void obs_h264_stop(void *data) {

};

static void clear_data(struct obs_h264 *obsx264){
	if (obsx264->context) {
		WelsDestroySVCEncoder(obsx264->context);
		bfree(obsx264->sei);
		bfree(obsx264->extra_data);

		obsx264->context = NULL;
		obsx264->sei = NULL;
		obsx264->extra_data = NULL;
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

static void obs_h264_defaults(obs_data_t *settings){
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



static obs_properties_t *obs_h264_props(void *unused){
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *list;
	obs_property_t *p;



	obs_properties_add_int(props, "bitrate", TEXT_BITRATE, 50, 10000000, 1);



#ifdef ENABLE_VFR
	obs_properties_add_bool(props, "vfr", TEXT_VFR);
#endif

	obs_properties_add_text(props, "x264opts", TEXT_X264_OPTS,
		OBS_TEXT_DEFAULT);
	return props;
}

static bool getparam(const char *param, char **name, const char **value){
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

static const char *validate(struct obs_h264 *obsx264,const char *val, const char *name,	const char *const *list){
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

static void override_base_param(struct obs_h264 *obsx264, const char *param,char **preset, char **profile, char **tune){
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

static inline void override_base_params(struct obs_h264 *obsx264, char **params,char **preset, char **profile, char **tune){
	while (*params)
		override_base_param(obsx264, *(params++),preset, profile, tune);
}


static inline void set_param(struct obs_h264 *obsx264, const char *param){
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

static inline void apply_h264_profile(struct obs_h264 *obsx264,	const char *profile){
	if (!obsx264->context && profile && *profile) {
		//int ret = x264_param_apply_profile(&obsx264->params, profile);
		//if (ret != 0)
		//	warn("Failed to set x264 profile '%s'", profile);
	}
}

static inline const char *validate_preset(struct obs_h264 *obsx264,const char *preset){
	//const char *new_preset = validate(obsx264, preset, "preset",
	//	x264_preset_names);
	//return new_preset ? new_preset : "veryfast";
	return "";
}

static bool reset_x264_params(struct obs_h264 *obsx264,	const char *preset, const char *tune){
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

		obsx264->params.i_bitrate         = bitrate;
		obsx264->params.i_width              = width;
		obsx264->params.i_height             = height;
		obsx264->params.i_fps_num            = voi->fps_num;
		obsx264->params.i_fps_den            = voi->fps_den;
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

static void load_headers(struct obs_h264 *obsx264)
{
	//x264_nal_t      *nals;
	int             nal_count = 0;
	DARRAY(uint8_t) header;
	DARRAY(uint8_t) sei;

	da_init(header);
	da_init(sei);

	//x264_encoder_headers(obsx264->context, &nals, &nal_count);

	for (int i = 0; i < nal_count; i++) {
		//x264_nal_t *nal = nals+i;

		//if (nal->i_type == NAL_SEI)
		//	da_push_back_array(sei, nal->p_payload, nal->i_payload);
		//else
		//	da_push_back_array(header, nal->p_payload,
		//			nal->i_payload);
	}

	obsx264->extra_data = header.array;
	obsx264->extra_data_size = header.num;
	obsx264->sei = sei.array;
	obsx264->sei_size = sei.num;
}


static int FillSpecificParameters(SEncParamExt* sParam,const h264_param_t* par) {
	/* Test for temporal, spatial, SNR scalability */
	sParam->iUsageType = SCREEN_CONTENT_REAL_TIME;
	sParam->fMaxFrameRate = par->i_frame_reference;                // input frame rate
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
	sParam->eSpsPpsIdStrategy = INCREASING_ID;
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
	sParam->sSpatialLayers[iIndexLayer].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

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

}

static void *obs_h264_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	struct obs_h264 *obsx264 = bzalloc(sizeof(struct obs_h264));
	obsx264->encoder = encoder;

	if (update_settings(obsx264, settings)) {
		//obsx264->context = x264_encoder_open(&obsx264->params);
		int iRet = WelsCreateSVCEncoder(&obsx264->context);
		if (iRet) {
			warn("WelsCreateSVCEncoder() failed!!");
		}

		if (obsx264->context == NULL)
			warn("x264 failed to load");
		else
			load_headers(obsx264);
	}
	else {
		warn("bad settings specified");
	}

	if (!obsx264->context) {
		bfree(obsx264);
		return NULL;
	}

	memset(&obsx264->sFbi, 0, sizeof(SFrameBSInfo));
	ISVCEncoder pPtrEnc = *obsx264->context;
	pPtrEnc->GetDefaultParams(obsx264->context,&obsx264->sSvcParam);
	
	//obsx264->context->GetDefaultParams(&obsx264->sSvcParam);
	

	FillSpecificParameters(&obsx264->sSvcParam,&obsx264->params );
	//pSrcPic = new SSourcePicture;
	//if (pSrcPic == NULL) {
	//	iRet = 1;
	//	goto INSIDE_MEM_FREE;
	//}
	//fill default pSrcPic
	//pSrcPic->iColorFormat = videoFormatI420;
	//pSrcPic->uiTimeStamp = 0;


	obsx264->performance_token =
		os_request_high_performance("x264 encoding");

	return obsx264;
}
typedef struct {
	int a;

} x264_nal_t;

typedef struct {
	int a;

} x264_picture_t;


static void parse_packet(struct obs_h264 *obsx264,
	struct encoder_packet *packet, x264_nal_t *nals,
	int nal_count, x264_picture_t *pic_out)
{
	if (!nal_count) return;

	da_resize(obsx264->packet_data, 0);

	for (int i = 0; i < nal_count; i++) {
		x264_nal_t *nal = nals + i;
		//da_push_back_array(obsx264->packet_data, nal->p_payload,
		//		nal->i_payload);
	}

	packet->data = obsx264->packet_data.array;
	packet->size = obsx264->packet_data.num;
	packet->type = OBS_ENCODER_VIDEO;
	//packet->pts           = pic_out->i_pts;
	//packet->dts           = pic_out->i_dts;
	//packet->keyframe      = pic_out->b_keyframe != 0;
}

static inline void init_pic_data(struct obs_h264 *obsx264, x264_picture_t *pic,
	struct encoder_frame *frame)
{
	//x264_picture_init(pic);
	/*
	pic->i_pts = frame->pts;
	pic->img.i_csp = obsx264->params.i_csp;

	if (obsx264->params.i_csp == X264_CSP_NV12)
		pic->img.i_plane = 2;
	else if (obsx264->params.i_csp == X264_CSP_I420)
		pic->img.i_plane = 3;
	else if (obsx264->params.i_csp == X264_CSP_I444)
		pic->img.i_plane = 3;

	for (int i = 0; i < pic->img.i_plane; i++) {
		pic->img.i_stride[i] = (int)frame->linesize[i];
		pic->img.plane[i]    = frame->data[i];
	}
	*/
}

// data 表示参数 frame表示需要编码的原始数据。packet表示输出的编码码流。received_packet表示本帧数据是否有编码输出。
static bool obs_h264_encode(void *data, struct encoder_frame *frame,struct encoder_packet *packet, bool *received_packet){
	struct obs_h264 *obsx264 = data;
	//x264_nal_t      *nals;
	//int             nal_count;
	//int             ret;
	//x264_picture_t  pic, pic_out;

	if (!frame || !packet || !received_packet)
		return false;

	//if (frame)
	//	init_pic_data(obsx264, &pic, frame);

	//ret = x264_encoder_encode(obsx264->context, &nals, &nal_count,
	//		(frame ? &pic : NULL), &pic_out);
	//if (ret < 0) {
	//	warn("encode failed");
	//	return false;
	//}

	//*received_packet = (nal_count != 0);
	//parse_packet(obsx264, packet, nals, nal_count, &pic_out);

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
