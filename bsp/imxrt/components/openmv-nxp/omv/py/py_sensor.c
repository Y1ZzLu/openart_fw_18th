/*
 * This file is part of the OpenMV project.
 *
 * Copyright (c) 2013-2019 Ibrahim Abdelkader <iabdalkader@openmv.io>
 * Copyright (c) 2013-2019 Kwabena W. Agyeman <kwagyeman@openmv.io>
 *
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Sensor Python module.
 */
#include <stdarg.h>
#include "mp.h"
#include "drivers/pin.h"
#include "sensor.h"
#include "imlib.h"
#include "xalloc.h"
#include "py_assert.h"
#include "py_image.h"
#include "py_imu.h"
#include "omv_boardconfig.h"
#include "py_helper.h"
#include "framebuffer.h"
#include "systick.h"
#include "drv_camera.h"
#define SENSOR_NAME "camera0"
extern sensor_t s_sensor;

static mp_obj_t py_sensor_reset() {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    PY_ASSERT_FALSE_MSG(sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_RESET,0) != 0, "Reset Failed");
    return mp_const_none;
}

static mp_obj_t py_sensor_sleep(mp_obj_t enable) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    PY_ASSERT_FALSE_MSG(sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SLEEP,mp_obj_is_true(enable)) != 0, "Sleep Failed");
    return mp_const_none;
}

static mp_obj_t py_sensor_shutdown(mp_obj_t enable) {
#ifdef SPECIAL_EFFECT	
    PY_ASSERT_FALSE_MSG(sensor_shutdown(mp_obj_is_true(enable)) != 0, "Shutdown Failed");
#else
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    PY_ASSERT_FALSE_MSG(sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SHUTDOWN,mp_obj_is_true(enable)) != 0, "Sleep Failed");
    return mp_const_none;
	//#warning "unsupport py_sensor_shutdown"
#endif	
    return mp_const_none;
}

static mp_obj_t py_sensor_flush() {
    fb_update_jpeg_buffer();
    return mp_const_none;
}

static mp_obj_t py_sensor_snapshot(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
#if MICROPY_PY_IMU
    // To prevent oscillation there is a 20 degree dead-zone between each state.
    // This means there is a 70 degree active-zone.
    if (sensor_get_auto_rotation()) {
        float pitch = py_imu_pitch_rotation();
        if (((pitch < 80) || (100 < pitch)) && ((pitch < 260) || (280 < pitch))) { // disable when 90 or 270
            float roll = py_imu_roll_rotation();
            if ((325 < roll) || (roll < 35) ) { // center is 0/360, upright
                sensor_set_hmirror(false);
                sensor_set_vflip(false);
                sensor_set_transpose(false);
            } else if ((235 < roll) && (roll < 305)) { // center is 270, rotated right
                sensor_set_hmirror(true);
                sensor_set_vflip(false);
                sensor_set_transpose(true);
            } else if ((145 < roll) && (roll < 215)) { // center is 180, upside down
                sensor_set_hmirror(true);
                sensor_set_vflip(true);
                sensor_set_transpose(false);
            } else if ((55 < roll) && (roll < 125)) { // center is 90, rotated left
                sensor_set_hmirror(false);
                sensor_set_vflip(true);
                sensor_set_transpose(true);
            }
        }
    }
#endif // MICROPY_PY_IMU
	
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
	if (sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SNAPSHOT,0) == -1) {
		nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "Sensor Timeout!!"));
        return mp_const_false;
    }
	// check args and kw_args
	mp_obj_t image;
	if(args || kw_args){
		image = py_image(0, 0, 0, 0);
		sensor->ops->get_frame(sensor, (image_t *) py_image_cobj(image));
	}else{
		sensor->ops->get_frame(sensor, NULL);
		image = mp_const_none;
	}
		
	
    return image;
}

static mp_obj_t py_sensor_skip_frames(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    mp_map_elem_t *kw_arg = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_time), MP_MAP_LOOKUP);
    mp_int_t time = 300; // OV Recommended.

    if (kw_arg != NULL) {
        time = mp_obj_get_int(kw_arg->value);
    }

    uint32_t millis = systick_current_millis();

    if (!n_args) {
        while ((systick_current_millis() - millis) < time) { // 32-bit math handles wrap arrounds...
            py_sensor_snapshot(0, NULL, NULL);
        }
    } else {
        for (int i = 0, j = mp_obj_get_int(args[0]); i < j; i++) {
            if ((kw_arg != NULL) && ((systick_current_millis() - millis) >= time)) {
                break;
            }

            py_sensor_snapshot(0, NULL, NULL);
        }
    }

    return mp_const_none;
}

static mp_obj_t py_sensor_release_frame(mp_obj_t img){
	image_t *in = (image_t *) py_image_cobj(img);
	uint32_t *ptr = (uint32_t *)in->pixels;
	// the lock is just ahead the ptr;
	*(--ptr) = 0;
	return mp_const_none;
}


static mp_obj_t py_sensor_width()
{
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    return mp_obj_new_int(imxrt_camera_width(sensor));
}

static mp_obj_t py_sensor_height()
{
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    return mp_obj_new_int(imxrt_camera_height(sensor));
}

static mp_obj_t py_sensor_get_fb()
{
    if (MAIN_FB()->bpp == 0) {
        return mp_const_none;
    }

    image_t image = {
        .w      = MAIN_FB()->w,
        .h      = MAIN_FB()->h,
        .bpp    = MAIN_FB()->bpp,
        .pixels = MAIN_FB()->pixels
    };

    return py_image_from_struct(&image);
}

static mp_obj_t py_sensor_get_id() {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    return mp_obj_new_int(imxrt_camera_chip_id(sensor));
}

static mp_obj_t py_sensor_alloc_extra_fb(mp_obj_t w_obj, mp_obj_t h_obj, mp_obj_t type_obj)
{
    int w = mp_obj_get_int(w_obj);
    PY_ASSERT_TRUE_MSG(w > 0, "Width must be > 0");

    int h = mp_obj_get_int(h_obj);
    PY_ASSERT_TRUE_MSG(h > 0, "Height must be > 0");

    int type = mp_obj_get_int(type_obj);
    PY_ASSERT_TRUE_MSG(type > 0, "Type must be > 0");

    image_t img = {
        .w      = w,
        .h      = h,
        .bpp    = type,
        .pixels = 0
    };

    switch(img.bpp) {
        case PIXFORMAT_BINARY:
            img.bpp = IMAGE_BPP_BINARY;
            break;
        case PIXFORMAT_GRAYSCALE:
            img.bpp = IMAGE_BPP_GRAYSCALE;
            break;
        case PIXFORMAT_RGB565:
            img.bpp = IMAGE_BPP_RGB565;
            break;
        case PIXFORMAT_BAYER:
            img.bpp = IMAGE_BPP_BAYER;
            break;
        default:
            break;
    }

    // Alloc image first (could fail) then alloc RAM so that there's no leak on failure.
    mp_obj_t r = py_image_from_struct(&img);

    fb_alloc_mark();
    ((image_t *) py_image_cobj(r))->pixels = fb_alloc0(image_size(&img), FB_ALLOC_NO_HINT);
    return r;
}

static mp_obj_t py_sensor_dealloc_extra_fb()
{
    fb_alloc_free_till_mark();
    return mp_const_none;
}

static mp_obj_t py_sensor_set_pixformat(mp_obj_t pixformat) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SET_PIXFORMAT,mp_obj_get_int(pixformat)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Pixel format is not supported!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_pixformat() {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
	int pixformat = imxrt_camera_pixformat(sensor);
    if (pixformat == PIXFORMAT_INVALID) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Pixel format not set yet!"));
    }
    return mp_obj_new_int(pixformat);
}

static mp_obj_t py_sensor_set_framerate(mp_obj_t framerate) {
    framerate_t fr;
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    switch (mp_obj_get_int(framerate)) {
        case 2:
            fr = FRAMERATE_2FPS;
            break;
        case 8:
            fr = FRAMERATE_8FPS;
            break;
        case 15:
            fr = FRAMERATE_15FPS;
            break;
        case 30:
            fr = FRAMERATE_30FPS;
            break;
        case 60:
            fr = FRAMERATE_60FPS;
            break;
        default:
			fr = mp_obj_get_int(framerate) | 0x80000000;	// encoding of CCM->CSCDR3 register
//            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Invalid framerate"));
            break;
    }
	
	if(sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SET_FRAMERATE,fr) != 0) 
	{
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_framesize(mp_obj_t framesize) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    if (sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SET_FRAMESIZE,mp_obj_get_int(framesize)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Failed to set framesize!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_framesize() {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
	int framesize = imxrt_camera_framesize(sensor);
	
    if (framesize == FRAMESIZE_INVALID) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Frame size not set yet!"));
    }
    return mp_obj_new_int(framesize);
}

static mp_obj_t py_sensor_set_windowing(mp_obj_t roi_obj)
{
    int x, y, w, h;
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
	int framesize = imxrt_camera_framesize(sensor);
	
    int res_w = resolution[framesize][0];
    int res_h = resolution[framesize][1];

    mp_obj_t *array;
    mp_uint_t array_len;
    mp_obj_get_array(roi_obj, &array_len, &array);

    if (array_len == 4) {
        x = mp_obj_get_int(array[0]);
        y = mp_obj_get_int(array[1]);
        w = mp_obj_get_int(array[2]);
        h = mp_obj_get_int(array[3]);
    } else if (array_len == 2) {
        w = mp_obj_get_int(array[0]);
        h = mp_obj_get_int(array[1]);
        x = (res_w / 2) - (w / 2);
        y = (res_h / 2) - (h / 2);
    } else {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError,
            "The tuple/list must either be (x, y, w, h) or (w, h)"));
    }

    if (w < 8 || h < 8) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError,
            "The selected window is too small"));
    }

    if (x < 0 || (x + w) > res_w || y < 0 || (y + h) > res_h) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError,
            "The selected window is outside the bounds of the frame"));
    }

    if (imxrt_camera_set_windowing(sensor, x, y, w, h) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Failed to set windowing!"));
    }

    return mp_const_none;
}

static mp_obj_t py_sensor_get_windowing()
{
    return mp_obj_new_tuple(4, (mp_obj_t []) {mp_obj_new_int(MAIN_FB()->x),
                                              mp_obj_new_int(MAIN_FB()->y),
                                              mp_obj_new_int(MAIN_FB()->u),
                                              mp_obj_new_int(MAIN_FB()->v)});
}

static mp_obj_t py_sensor_set_gainceiling(mp_obj_t gainceiling) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    gainceiling_t gain;
    switch (mp_obj_get_int(gainceiling)) {
        case 2:
            gain = GAINCEILING_2X;
            break;
        case 4:
            gain = GAINCEILING_4X;
            break;
        case 8:
            gain = GAINCEILING_8X;
            break;
        case 16:
            gain = GAINCEILING_16X;
            break;
        case 32:
            gain = GAINCEILING_32X;
            break;
        case 64:
            gain = GAINCEILING_64X;
            break;
        case 128:
            gain = GAINCEILING_128X;
            break;
        default:
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Invalid gainceiling"));
            break;
    }

    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SET_GAINCEILING, gain) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_brightness(mp_obj_t brightness) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETBRIGHTNESS, mp_obj_get_int(brightness)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_contrast(mp_obj_t contrast) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SET_CONTRAST, mp_obj_get_int(contrast)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_saturation(mp_obj_t saturation) {
	
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETSATURATION,mp_obj_get_int(saturation)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_quality(mp_obj_t qs) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    int q = mp_obj_get_int(qs);
    PY_ASSERT_TRUE((q >= 0 && q <= 100));

    q = 100-q; //invert quality
    q = 255*q/100; //map to 0->255
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETQUALITY,q) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_colorbar(mp_obj_t enable) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETCOLORBAR,mp_obj_is_true(enable)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_auto_gain(uint n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    int enable = mp_obj_get_int(args[0]);
    float gain_db = py_helper_keyword_float(n_args, args, 1, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_gain_db), NAN);
    float gain_db_ceiling = py_helper_keyword_float(n_args, args, 2, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_gain_db_ceiling), NAN);
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    if (imxrt_camera_set_auto_gain(sensor, enable, gain_db, gain_db_ceiling) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_gain_db() {
    float gain_db = 0;
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (imxrt_camera_sensor_get_gain_db(sensor, &gain_db) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_obj_new_float(gain_db);
}

static mp_obj_t py_sensor_set_auto_exposure(uint n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    int exposure_us = py_helper_keyword_int(n_args, args, 1, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_exposure_us), -1);
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (imxrt_camera_set_auto_exposure(sensor, mp_obj_get_int(args[0]), exposure_us) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_exposure_us() {
    int exposure_us = 0;
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (imxrt_camera_get_exposure_us(sensor, &exposure_us) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_obj_new_int(exposure_us);
}

static mp_obj_t py_sensor_set_auto_whitebal(uint n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    int enable = mp_obj_get_int(args[0]);
    float rgb_gain_db[3] = {NAN, NAN, NAN};
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    py_helper_keyword_float_array(n_args, args, 1, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_rgb_gain_db), rgb_gain_db, 3);
    if (imxrt_camera_set_auto_whitebal(sensor, enable, rgb_gain_db[0], rgb_gain_db[1], rgb_gain_db[2]) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_rgb_gain_db() {
    float r_gain_db = 0.0, g_gain_db = 0.0, b_gain_db = 0.0;
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (imxrt_camera_get_rgb_gain_db(sensor, &r_gain_db, &g_gain_db, &b_gain_db) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_obj_new_tuple(3, (mp_obj_t []) {mp_obj_new_float(r_gain_db), mp_obj_new_float(g_gain_db), mp_obj_new_float(b_gain_db)});
}

static mp_obj_t py_sensor_set_hmirror(mp_obj_t enable) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETHMIRROR, mp_obj_is_true(enable)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_hmirror() {
#ifdef SPECIAL_EFFECT	
    return mp_obj_new_bool(sensor_get_hmirror());
#else
//	#warning "unsupport py_sensor_get_hmirror"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_set_vflip(mp_obj_t enable) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SET_VFLIP,mp_obj_is_true(enable)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_vflip() {
#ifdef SPECIAL_EFFECT	
    return mp_obj_new_bool(sensor_get_vflip());
#else
	//#warning "unsupport py_sensor_get_vflip"
#endif		
	return mp_const_none;
}

static mp_obj_t py_sensor_set_transpose(mp_obj_t enable) {
#ifdef SPECIAL_EFFECT	
    if (sensor_set_transpose(mp_obj_is_true(enable)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Cannot transpose in JPEG mode!"));
    }
    return mp_const_none;
#else
	//#warning "unsupport py_sensor_set_transpose"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_get_transpose() {
#ifdef SPECIAL_EFFECT	
    return mp_obj_new_bool(sensor_get_transpose());
#else
	//#warning "unsupport py_sensor_get_transpose"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_set_auto_rotation(mp_obj_t enable) {
#ifdef SPECIAL_EFFECT		
    if (sensor_set_auto_rotation(mp_obj_is_true(enable)) != 0) {
        nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Cannot auto rotate in JPEG mode!"));
    }
    return mp_const_none;
#else
	//#warning "unsupport py_sensor_set_auto_rotation"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_get_auto_rotation() {
#ifdef SPECIAL_EFFECT	
    return mp_obj_new_bool(sensor_get_auto_rotation());
#else
	//#warning "unsupport py_sensor_get_auto_rotation"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_set_special_effect(mp_obj_t sde) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    if (sensor->ops->camera_control(sensor, RT_DRV_CAM_CMD_SETSPECIALEFFECT,mp_obj_get_int(sde)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_lens_correction(mp_obj_t enable, mp_obj_t radi, mp_obj_t coef) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
		
    if (imxrt_camera_set_lens_correction(sensor, mp_obj_is_true(enable),
                mp_obj_get_int(radi), mp_obj_get_int(coef)) != 0) {
        return mp_const_false;
    }
    return mp_const_true;
}

static mp_obj_t py_sensor_set_vsync_output(mp_obj_t pin_obj) {
#ifdef SPECIAL_EFFECT		
    pin_obj_t *pin = pin_obj;
    sensor_set_vsync_output(pin->gpio, pin->pin_mask);
    return mp_const_true;
#else
	//#warning "unsupport py_sensor_set_vsync_output"
#endif	
	return mp_const_none;
}

static mp_obj_t py_sensor_ioctl(uint n_args, const mp_obj_t *args)
{
    mp_obj_t ret_obj = mp_const_none;
    int request = mp_obj_get_int(args[0]);
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
	
    switch (request) {
        case IOCTL_SET_TRIGGERED_MODE:
            if (n_args < 2 || imxrt_camera_ioctl(sensor, request, mp_obj_get_int(args[1])) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            break;

        case IOCTL_GET_TRIGGERED_MODE: {
            int enabled;
            if (imxrt_camera_ioctl(sensor, request, &enabled) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_bool(enabled);
            break;
        }

        case IOCTL_LEPTON_GET_WIDTH: {
            int width;
            if (imxrt_camera_ioctl(sensor, request, &width) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_int(width);
            break;
        }

        case IOCTL_LEPTON_GET_HEIGHT: {
            int height;
            if (imxrt_camera_ioctl(sensor, request, &height) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_int(height);
            break;
        }

        case IOCTL_LEPTON_GET_RADIOMETRY: {
            int radiometry;
            if (imxrt_camera_ioctl(sensor, request, &radiometry) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_int(radiometry);
            break;
        }

        case IOCTL_LEPTON_GET_REFRESH: {
            int refresh;
            if (imxrt_camera_ioctl(sensor, request, &refresh) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_int(refresh);
            break;
        }

        case IOCTL_LEPTON_GET_RESOLUTION: {
            int resolution;
            if (imxrt_camera_ioctl(sensor, request, &resolution) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_int(resolution);
            break;
        }

        case IOCTL_LEPTON_RUN_COMMAND: {
            if (n_args < 2 || imxrt_camera_ioctl(sensor, request, mp_obj_get_int(args[1])) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            break;
        }

        case IOCTL_LEPTON_SET_ATTRIBUTE: {
            size_t data_len;
            int command = mp_obj_get_int(args[1]);
            uint16_t *data = (uint16_t *) mp_obj_str_get_data(args[2], &data_len);
            PY_ASSERT_TRUE_MSG(data_len > 0, "0 bytes transferred!");
            if (imxrt_camera_ioctl(sensor, request, command, data, data_len / sizeof(uint16_t)) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            break;
        }

        case IOCTL_LEPTON_GET_ATTRIBUTE: {
            int command = mp_obj_get_int(args[1]);
            size_t data_len = mp_obj_get_int(args[2]);
            PY_ASSERT_TRUE_MSG(data_len > 0, "0 bytes transferred!");
            uint16_t *data = xalloc(data_len * sizeof(uint16_t));
            if (imxrt_camera_ioctl(sensor, request, command, data, data_len) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_bytearray_by_ref(data_len * sizeof(uint16_t), data);
            break;
        }

        case IOCTL_LEPTON_GET_FPA_TEMPERATURE:
        case IOCTL_LEPTON_GET_AUX_TEMPERATURE: {
            int temp;
            if (imxrt_camera_ioctl(sensor, request, &temp) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_float((((float) temp) / 100) - 273.15f);
            break;
        }

        case IOCTL_LEPTON_SET_MEASUREMENT_MODE:
            if (n_args < 2 || imxrt_camera_ioctl(sensor, request, mp_obj_get_int(args[1])) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            break;

        case IOCTL_LEPTON_GET_MEASUREMENT_MODE: {
            int enabled;
            if (imxrt_camera_ioctl(sensor, request, &enabled) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_bool(enabled);
            break;
        }

        case IOCTL_LEPTON_SET_MEASUREMENT_RANGE:
            if (n_args < 3) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            // GCC will not less us pass floats to ... so we have to pass float pointers instead.
            float min = mp_obj_get_float(args[1]);
            float max = mp_obj_get_float(args[2]);
            if (imxrt_camera_ioctl(sensor, request, &min, &max) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            break;

        case IOCTL_LEPTON_GET_MEASUREMENT_RANGE: {
            float min, max;
            if (imxrt_camera_ioctl(sensor, request, &min, &max) != 0) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Sensor control failed!"));
            }
            ret_obj = mp_obj_new_tuple(2, (mp_obj_t []) {mp_obj_new_float(min), mp_obj_new_float(max)});
            break;
        }

        default: {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Operation not supported!"));
            break;
        }
    }
    return ret_obj;
}

static mp_obj_t py_sensor_set_color_palette(mp_obj_t palette_obj) {
    int palette = mp_obj_get_int(palette_obj);
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    switch (palette) {
        case COLOR_PALETTE_RAINBOW:

			sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SETCOLORPALETTE,(uint32_t)rainbow_table);
            break;
        case COLOR_PALETTE_IRONBOW:

			sensor->ops->camera_control(sensor,RT_DRV_CAM_CMD_SETCOLORPALETTE,(uint32_t)ironbow_table);
            break;
        default:
            nlr_raise(mp_obj_new_exception_msg(&mp_type_ValueError, "Invalid color palette!"));
            break;
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_get_color_palette() {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    const uint16_t *palette = imxrt_camera_get_color_palette(sensor);
    if (palette == rainbow_table) {
        return mp_obj_new_int(COLOR_PALETTE_RAINBOW);
    } else if (palette == ironbow_table) {
        return mp_obj_new_int(COLOR_PALETTE_IRONBOW);
    }
    return mp_const_none;
}

static mp_obj_t py_sensor_write_reg(mp_obj_t addr, mp_obj_t val) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    imxrt_camera_write_reg(sensor, mp_obj_get_int(addr), mp_obj_get_int(val));
    return mp_const_none;
}

static mp_obj_t py_sensor_read_reg(mp_obj_t addr) {
	struct rt_camera_device *sensor = imxrt_camera_device_find(SENSOR_NAME);
    return mp_obj_new_int(imxrt_camera_read_reg(sensor, mp_obj_get_int(addr)));
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_reset_obj,               py_sensor_reset);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_sleep_obj,               py_sensor_sleep);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_shutdown_obj,            py_sensor_shutdown);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_flush_obj,               py_sensor_flush);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_snapshot_obj, 0,        py_sensor_snapshot);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_skip_frames_obj, 0,     py_sensor_skip_frames);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_release_frame_obj,       py_sensor_release_frame);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_width_obj,               py_sensor_width);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_height_obj,              py_sensor_height);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_fb_obj,              py_sensor_get_fb);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_id_obj,              py_sensor_get_id);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(py_sensor_alloc_extra_fb_obj,      py_sensor_alloc_extra_fb);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_dealloc_extra_fb_obj,    py_sensor_dealloc_extra_fb);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_pixformat_obj,       py_sensor_set_pixformat);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_pixformat_obj,       py_sensor_get_pixformat);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_framesize_obj,       py_sensor_set_framesize);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_framesize_obj,       py_sensor_get_framesize);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_framerate_obj,       py_sensor_set_framerate);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_windowing_obj,       py_sensor_set_windowing);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_windowing_obj,       py_sensor_get_windowing);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_gainceiling_obj,     py_sensor_set_gainceiling);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_contrast_obj,        py_sensor_set_contrast);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_brightness_obj,      py_sensor_set_brightness);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_saturation_obj,      py_sensor_set_saturation);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_quality_obj,         py_sensor_set_quality);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_colorbar_obj,        py_sensor_set_colorbar);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_set_auto_gain_obj,    1,py_sensor_set_auto_gain);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_gain_db_obj,         py_sensor_get_gain_db);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_set_auto_exposure_obj,1,py_sensor_set_auto_exposure);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_exposure_us_obj,     py_sensor_get_exposure_us);
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_sensor_set_auto_whitebal_obj,1,py_sensor_set_auto_whitebal);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_rgb_gain_db_obj,     py_sensor_get_rgb_gain_db);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_hmirror_obj,         py_sensor_set_hmirror);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_hmirror_obj,         py_sensor_get_hmirror);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_vflip_obj,           py_sensor_set_vflip);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_vflip_obj,           py_sensor_get_vflip);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_transpose_obj,       py_sensor_set_transpose);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_transpose_obj,       py_sensor_get_transpose);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_auto_rotation_obj,   py_sensor_set_auto_rotation);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_auto_rotation_obj,   py_sensor_get_auto_rotation);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_special_effect_obj,  py_sensor_set_special_effect);
STATIC MP_DEFINE_CONST_FUN_OBJ_3(py_sensor_set_lens_correction_obj, py_sensor_set_lens_correction);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_vsync_output_obj,    py_sensor_set_vsync_output);
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_sensor_ioctl_obj, 1, 5, py_sensor_ioctl);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_set_color_palette_obj,   py_sensor_set_color_palette);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_sensor_get_color_palette_obj,   py_sensor_get_color_palette);
STATIC MP_DEFINE_CONST_FUN_OBJ_2(py_sensor_write_reg_obj,           py_sensor_write_reg);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_sensor_read_reg_obj,            py_sensor_read_reg);

extern void usb_write_open();
mp_obj_t py_usb_write_open(){
	usb_write_open();
	return mp_const_none;
}
extern void usb_write_close();
mp_obj_t py_usb_write_close(){
	usb_write_close();
	return mp_const_none;
}

STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_usb_write_open_obj,       py_usb_write_open);
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_usb_write_close_obj,       py_usb_write_close);

STATIC const mp_map_elem_t globals_dict_table[] = {
	{ MP_OBJ_NEW_QSTR(MP_QSTR_open),                (mp_obj_t)(&py_usb_write_open_obj)},
	{ MP_OBJ_NEW_QSTR(MP_QSTR_close),                (mp_obj_t)(&py_usb_write_close_obj)},	
    { MP_OBJ_NEW_QSTR(MP_QSTR___name__),            MP_OBJ_NEW_QSTR(MP_QSTR_sensor)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_BINARY),              MP_OBJ_NEW_SMALL_INT(PIXFORMAT_BINARY)},   /* 1BPP/BINARY*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_GRAYSCALE),           MP_OBJ_NEW_SMALL_INT(PIXFORMAT_GRAYSCALE)},/* 1BPP/GRAYSCALE*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_RGB565),              MP_OBJ_NEW_SMALL_INT(PIXFORMAT_RGB565)},   /* 2BPP/RGB565*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_YUV422),              MP_OBJ_NEW_SMALL_INT(PIXFORMAT_YUV422)},   /* 2BPP/YUV422*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_BAYER),               MP_OBJ_NEW_SMALL_INT(PIXFORMAT_BAYER)},    /* 1BPP/RAW*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_JPEG),                MP_OBJ_NEW_SMALL_INT(PIXFORMAT_JPEG)},     /* JPEG/COMPRESSED*/
    { MP_OBJ_NEW_QSTR(MP_QSTR_OV9650),              MP_OBJ_NEW_SMALL_INT(OV9650_ID)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_OV2640),              MP_OBJ_NEW_SMALL_INT(OV2640_ID)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_OV7725),              MP_OBJ_NEW_SMALL_INT(OV7725_ID)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_MT9V034),             MP_OBJ_NEW_SMALL_INT(MT9V034_ID)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_LEPTON),              MP_OBJ_NEW_SMALL_INT(LEPTON_ID)},

    // Special effects
    { MP_OBJ_NEW_QSTR(MP_QSTR_NORMAL),              MP_OBJ_NEW_SMALL_INT(SDE_NORMAL)},          /* Normal/No SDE */
    { MP_OBJ_NEW_QSTR(MP_QSTR_NEGATIVE),            MP_OBJ_NEW_SMALL_INT(SDE_NEGATIVE)},        /* Negative image */

    // C/SIF Resolutions
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQCIF),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQCIF)},    /* 88x72     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QCIF),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QCIF)},     /* 176x144   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_CIF),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_CIF)},      /* 352x288   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQSIF),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQSIF)},    /* 88x60     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QSIF),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QSIF)},     /* 176x120   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_SIF),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_SIF)},      /* 352x240   */
    // VGA Resolutions
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQQQVGA),             MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQQQVGA)},  /* 40x30     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQQVGA),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQQVGA)},   /* 80x60     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQVGA),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQVGA)},    /* 160x120   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QVGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QVGA)},     /* 320x240   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_VGA),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_VGA)},      /* 640x480   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_HQQQVGA),             MP_OBJ_NEW_SMALL_INT(FRAMESIZE_HQQQVGA)},  /* 80x40     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_HQQVGA),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_HQQVGA)},   /* 160x80    */
    { MP_OBJ_NEW_QSTR(MP_QSTR_HQVGA),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_HQVGA)},    /* 240x160   */
    // FFT Resolutions
    { MP_OBJ_NEW_QSTR(MP_QSTR_B64X32),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_64X32)},    /* 64x32     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_B64X64),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_64X64)},    /* 64x64     */
    { MP_OBJ_NEW_QSTR(MP_QSTR_B128X64),             MP_OBJ_NEW_SMALL_INT(FRAMESIZE_128X64)},   /* 128x64    */
    { MP_OBJ_NEW_QSTR(MP_QSTR_B128X128),            MP_OBJ_NEW_SMALL_INT(FRAMESIZE_128X128)},  /* 128x128   */
    // Other
    { MP_OBJ_NEW_QSTR(MP_QSTR_LCD),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_LCD)},      /* 128x160   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QQVGA2),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QQVGA2)},   /* 128x160   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_WVGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_WVGA)},     /* 720x480   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_WVGA2),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_WVGA2)},    /* 752x480   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_SVGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_SVGA)},     /* 800x600   */
    { MP_OBJ_NEW_QSTR(MP_QSTR_XGA),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_XGA)},      /* 1024x768  */
    { MP_OBJ_NEW_QSTR(MP_QSTR_SXGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_SXGA)},     /* 1280x1024 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_UXGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_UXGA)},     /* 1600x1200 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_HD),                  MP_OBJ_NEW_SMALL_INT(FRAMESIZE_HD)},       /* 1280x720  */
    { MP_OBJ_NEW_QSTR(MP_QSTR_FHD),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_FHD)},      /* 1920x1080 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QHD),                 MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QHD)},      /* 2560x1440 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_QXGA),                MP_OBJ_NEW_SMALL_INT(FRAMESIZE_QXGA)},     /* 2048x1536 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_WQXGA),               MP_OBJ_NEW_SMALL_INT(FRAMESIZE_WQXGA)},    /* 2560x1600 */
    { MP_OBJ_NEW_QSTR(MP_QSTR_WQXGA2),              MP_OBJ_NEW_SMALL_INT(FRAMESIZE_WQXGA2)},   /* 2592x1944 */

    // Color Palettes
    { MP_OBJ_NEW_QSTR(MP_QSTR_PALETTE_RAINBOW),     MP_OBJ_NEW_SMALL_INT(COLOR_PALETTE_RAINBOW)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_PALETTE_IRONBOW),     MP_OBJ_NEW_SMALL_INT(COLOR_PALETTE_IRONBOW)},

    // IOCTLs
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_SET_TRIGGERED_MODE),            MP_OBJ_NEW_SMALL_INT(IOCTL_SET_TRIGGERED_MODE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_GET_TRIGGERED_MODE),            MP_OBJ_NEW_SMALL_INT(IOCTL_GET_TRIGGERED_MODE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_WIDTH),              MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_WIDTH)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_HEIGHT),             MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_HEIGHT)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_RADIOMETRY),         MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_RADIOMETRY)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_REFRESH),            MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_REFRESH)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_RESOLUTION),         MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_RESOLUTION)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_RUN_COMMAND),            MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_RUN_COMMAND)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_SET_ATTRIBUTE),          MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_SET_ATTRIBUTE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_ATTRIBUTE),          MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_ATTRIBUTE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_FPA_TEMPERATURE),    MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_FPA_TEMPERATURE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_AUX_TEMPERATURE),    MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_AUX_TEMPERATURE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_SET_MEASUREMENT_MODE),   MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_SET_MEASUREMENT_MODE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_MEASUREMENT_MODE),   MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_MEASUREMENT_MODE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_SET_MEASUREMENT_RANGE),  MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_SET_MEASUREMENT_RANGE)},
    { MP_OBJ_NEW_QSTR(MP_QSTR_IOCTL_LEPTON_GET_MEASUREMENT_RANGE),  MP_OBJ_NEW_SMALL_INT(IOCTL_LEPTON_GET_MEASUREMENT_RANGE)},

    // Sensor functions
    { MP_OBJ_NEW_QSTR(MP_QSTR_reset),               (mp_obj_t)&py_sensor_reset_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_sleep),               (mp_obj_t)&py_sensor_sleep_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_shutdown),            (mp_obj_t)&py_sensor_shutdown_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_flush),               (mp_obj_t)&py_sensor_flush_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_snapshot),            (mp_obj_t)&py_sensor_snapshot_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_skip_frames),         (mp_obj_t)&py_sensor_skip_frames_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_release_frame),       (mp_obj_t)&py_sensor_release_frame_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_width),               (mp_obj_t)&py_sensor_width_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_height),              (mp_obj_t)&py_sensor_height_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_fb),              (mp_obj_t)&py_sensor_get_fb_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_id),              (mp_obj_t)&py_sensor_get_id_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_alloc_extra_fb),      (mp_obj_t)&py_sensor_alloc_extra_fb_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_dealloc_extra_fb),    (mp_obj_t)&py_sensor_dealloc_extra_fb_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_pixformat),       (mp_obj_t)&py_sensor_set_pixformat_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_pixformat),       (mp_obj_t)&py_sensor_get_pixformat_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_framesize),       (mp_obj_t)&py_sensor_set_framesize_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_framesize),       (mp_obj_t)&py_sensor_get_framesize_obj },
	{ MP_OBJ_NEW_QSTR(MP_QSTR_set_framerate),       (mp_obj_t)&py_sensor_set_framerate_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_windowing),       (mp_obj_t)&py_sensor_set_windowing_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_windowing),       (mp_obj_t)&py_sensor_get_windowing_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_gainceiling),     (mp_obj_t)&py_sensor_set_gainceiling_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_contrast),        (mp_obj_t)&py_sensor_set_contrast_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_brightness),      (mp_obj_t)&py_sensor_set_brightness_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_saturation),      (mp_obj_t)&py_sensor_set_saturation_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_quality),         (mp_obj_t)&py_sensor_set_quality_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_colorbar),        (mp_obj_t)&py_sensor_set_colorbar_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_auto_gain),       (mp_obj_t)&py_sensor_set_auto_gain_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_gain_db),         (mp_obj_t)&py_sensor_get_gain_db_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_auto_exposure),   (mp_obj_t)&py_sensor_set_auto_exposure_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_exposure_us),     (mp_obj_t)&py_sensor_get_exposure_us_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_auto_whitebal),   (mp_obj_t)&py_sensor_set_auto_whitebal_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_rgb_gain_db),     (mp_obj_t)&py_sensor_get_rgb_gain_db_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_hmirror),         (mp_obj_t)&py_sensor_set_hmirror_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_hmirror),         (mp_obj_t)&py_sensor_get_hmirror_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_vflip),           (mp_obj_t)&py_sensor_set_vflip_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_vflip),           (mp_obj_t)&py_sensor_get_vflip_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_transpose),       (mp_obj_t)&py_sensor_set_transpose_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_transpose),       (mp_obj_t)&py_sensor_get_transpose_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_auto_rotation),   (mp_obj_t)&py_sensor_set_auto_rotation_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_auto_rotation),   (mp_obj_t)&py_sensor_get_auto_rotation_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_special_effect),  (mp_obj_t)&py_sensor_set_special_effect_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_lens_correction), (mp_obj_t)&py_sensor_set_lens_correction_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_vsync_output),    (mp_obj_t)&py_sensor_set_vsync_output_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ioctl),               (mp_obj_t)&py_sensor_ioctl_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_set_color_palette),   (mp_obj_t)&py_sensor_set_color_palette_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_get_color_palette),   (mp_obj_t)&py_sensor_get_color_palette_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR___write_reg),         (mp_obj_t)&py_sensor_write_reg_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR___read_reg),          (mp_obj_t)&py_sensor_read_reg_obj },
};

STATIC MP_DEFINE_CONST_DICT(globals_dict, globals_dict_table);

const mp_obj_module_t sensor_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_t)&globals_dict,
};
