/* This file is part of the OpenMV project.
 * Copyright (c) 2013-2019 Ibrahim Abdelkader <iabdalkader@openmv.io> & Kwabena W. Agyeman <kwagyeman@openmv.io>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 */

#include <mp.h>
#include "py_helper.h"
#include "py_image.h"
#include "ff_wrapper.h"
#include "glow_bundle.h"


// glow Model Object
typedef struct py_glow_model_obj {
    mp_obj_base_t base;
    unsigned char *model_data;
    unsigned int model_data_len, height, width, channels;
    unsigned int out_height, out_width, out_channels;
	glow_engine_t *engine;
} py_glow_model_obj_t;

STATIC void py_glow_model_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    py_glow_model_obj_t *self = self_in;
    mp_printf(print,
              "{\"len\":%d, \"height\":%d, \"width\":%d, \"channels\":%d}",
              self->model_data_len,
              self->height,
              self->width);
}

// glow Classification Object
#define py_glow_classification_obj_size 5
typedef struct py_glow_classification_obj {
    mp_obj_base_t base;
    mp_obj_t x, y, w, h, output;
} py_glow_classification_obj_t;

STATIC void py_glow_classification_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    py_glow_classification_obj_t *self = self_in;
    mp_printf(print,
              "{\"x\":%d, \"y\":%d, \"w\":%d, \"h\":%d, \"output\":",
              mp_obj_get_int(self->x),
              mp_obj_get_int(self->y),
              mp_obj_get_int(self->w),
              mp_obj_get_int(self->h));
    mp_obj_print_helper(print, self->output, kind);
    mp_printf(print, "}");
}

STATIC mp_obj_t py_glow_classification_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value)
{
    if (value == MP_OBJ_SENTINEL) { // load
        py_glow_classification_obj_t *self = self_in;
        if (MP_OBJ_IS_TYPE(index, &mp_type_slice)) {
            mp_bound_slice_t slice;
            if (!mp_seq_get_fast_slice_indexes(py_glow_classification_obj_size, index, &slice)) {
                nlr_raise(mp_obj_new_exception_msg(&mp_type_OSError, "only slices with step=1 (aka None) are supported"));
            }
            mp_obj_tuple_t *result = mp_obj_new_tuple(slice.stop - slice.start, NULL);
            mp_seq_copy(result->items, &(self->x) + slice.start, result->len, mp_obj_t);
            return result;
        }
        switch (mp_get_index(self->base.type, py_glow_classification_obj_size, index, false)) {
            case 0: return self->x;
            case 1: return self->y;
            case 2: return self->w;
            case 3: return self->h;
            case 4: return self->output;
        }
    }
    return MP_OBJ_NULL; // op not supported
}

mp_obj_t py_glow_classification_rect(mp_obj_t self_in)
{
    return mp_obj_new_tuple(4, (mp_obj_t []) {((py_glow_classification_obj_t *) self_in)->x,
                                              ((py_glow_classification_obj_t *) self_in)->y,
                                              ((py_glow_classification_obj_t *) self_in)->w,
                                              ((py_glow_classification_obj_t *) self_in)->h});
}

mp_obj_t py_glow_classification_x(mp_obj_t self_in) { return ((py_glow_classification_obj_t *) self_in)->x; }
mp_obj_t py_glow_classification_y(mp_obj_t self_in) { return ((py_glow_classification_obj_t *) self_in)->y; }
mp_obj_t py_glow_classification_w(mp_obj_t self_in) { return ((py_glow_classification_obj_t *) self_in)->w; }
mp_obj_t py_glow_classification_h(mp_obj_t self_in) { return ((py_glow_classification_obj_t *) self_in)->h; }
mp_obj_t py_glow_classification_output(mp_obj_t self_in) { return ((py_glow_classification_obj_t *) self_in)->output; }

STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_rect_obj, py_glow_classification_rect);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_x_obj, py_glow_classification_x);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_y_obj, py_glow_classification_y);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_w_obj, py_glow_classification_w);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_h_obj, py_glow_classification_h);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_classification_output_obj, py_glow_classification_output);

STATIC const mp_rom_map_elem_t py_glow_classification_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&py_glow_classification_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_x), MP_ROM_PTR(&py_glow_classification_x_obj) },
    { MP_ROM_QSTR(MP_QSTR_y), MP_ROM_PTR(&py_glow_classification_y_obj) },
    { MP_ROM_QSTR(MP_QSTR_w), MP_ROM_PTR(&py_glow_classification_w_obj) },
    { MP_ROM_QSTR(MP_QSTR_h), MP_ROM_PTR(&py_glow_classification_h_obj) },
    { MP_ROM_QSTR(MP_QSTR_output), MP_ROM_PTR(&py_glow_classification_output_obj) }
};

STATIC MP_DEFINE_CONST_DICT(py_glow_classification_locals_dict, py_glow_classification_locals_dict_table);

static const mp_obj_type_t py_glow_classification_type = {
    { &mp_type_type },
    .name  = MP_QSTR_glow_classification,
    .print = py_glow_classification_print,
    .subscr = py_glow_classification_subscr,
    .locals_dict = (mp_obj_t) &py_glow_classification_locals_dict
};

static const mp_obj_type_t py_glow_model_type;

STATIC mp_obj_t int_py_glow_load(mp_obj_t path_obj, bool alloc_mode, bool load_once)
{
    const char *path = mp_obj_str_get_str(path_obj);
    py_glow_model_obj_t *glow_model = m_new_obj(py_glow_model_obj_t);
    glow_model->base.type = &py_glow_model_type;

	FIL fp;
	file_read_open(&fp, path);
	glow_model->model_data_len = f_size(&fp);
	glow_model->model_data = alloc_mode
		? fb_alloc(glow_model->model_data_len, FB_ALLOC_PREFER_SIZE)
		: xalloc(glow_model->model_data_len);
	read_data(&fp, glow_model->model_data, glow_model->model_data_len);
	file_close(&fp);


	glow_engine_t* engine = m_new_obj(glow_engine_t);
	INIT_GLOW_ENGINE(engine, glow_model->model_data);
	engine->entry = (func_ptr)(glow_model->model_data + 64 + 1);
	
	glow_model->height = engine->shape[1];
	glow_model->width = engine->shape[2];
	glow_model->channels = engine->shape[3];
	
	glow_model->out_height = engine->out_shape[1];
	glow_model->out_width = engine->out_shape[2];
	glow_model->out_channels = engine->out_shape[3];
	
	glow_model->engine = engine;

	if (alloc_mode && load_once) {
        fb_alloc_mark_permanent(); // glow_model->model_data will not be popped on exception.
    }

    return glow_model;
}

STATIC mp_obj_t py_glow_load(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{
    return int_py_glow_load(args[0], py_helper_keyword_int(n_args, args, 1, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_load_to_fb), false), true);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_glow_load_obj, 1, py_glow_load);

STATIC mp_obj_t py_glow_free_from_fb()
{
    fb_alloc_free_till_mark_past_mark_permanent();
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(py_glow_free_from_fb_obj, py_glow_free_from_fb);

STATIC py_glow_model_obj_t *py_glow_load_alloc(mp_obj_t path_obj)
{
    if (MP_OBJ_IS_TYPE(path_obj, &py_glow_model_type)) {
        return (py_glow_model_obj_t *) path_obj;
    } else {
        return (py_glow_model_obj_t *) int_py_glow_load(path_obj, true, false);
    }
}

typedef struct py_glow_input_data_callback_data {
    image_t *img;
    rectangle_t *roi;
	int offset, scale;
} py_glow_input_data_callback_data_t;

STATIC void py_glow_input_data_callback(void *callback_data,
                                      void *model_input,
                                      const unsigned int input_height,
                                      const unsigned int input_width,
                                      const unsigned int input_channels)
{

    py_glow_input_data_callback_data_t *arg = (py_glow_input_data_callback_data_t *) callback_data;
    float fscale = 1.0f / (arg->scale);
	float offset = arg->offset * fscale;

    float xscale = input_width / ((float) arg->roi->w);
    float yscale = input_height / ((float) arg->roi->h);
    // MAX == KeepAspectRationByExpanding - MIN == KeepAspectRatio
    float scale = IM_MAX(xscale, yscale), scale_inv = 1 / scale;
    float x_offset = ((arg->roi->w * scale) - input_width) / 2;
    float y_offset = ((arg->roi->h * scale) - input_height) / 2;

    switch (arg->img->bpp) {
        case IMAGE_BPP_BINARY: {
            for (int y = 0, yy = input_height; y < yy; y++) {
                uint32_t *row_ptr = IMAGE_COMPUTE_BINARY_PIXEL_ROW_PTR(arg->img, fast_floorf((y + y_offset) * scale_inv) + arg->roi->y);
                int row = input_width * y;
                for (int x = 0, xx = input_width; x < xx; x++) {
                    int pixel = IMAGE_GET_BINARY_PIXEL_FAST(row_ptr, fast_floorf((x + x_offset) * scale_inv) + arg->roi->x);
                    int index = row + x;
                    switch (input_channels) {
                        case 1: {
                            ((float *) model_input)[index] = COLOR_BINARY_TO_GRAYSCALE(pixel) * fscale - offset;
                            break;
                        }
                        case 3: {
                            int index_3 = index * 3;
                            pixel = COLOR_BINARY_TO_RGB565(pixel);
							((float *) model_input)[index_3 + 0] = COLOR_RGB565_TO_R8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 1] = COLOR_RGB565_TO_G8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 2] = COLOR_RGB565_TO_B8(pixel) * fscale - offset;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
            }
            break;
        }
        case IMAGE_BPP_GRAYSCALE: {
            for (int y = 0, yy = input_height; y < yy; y++) {
                uint8_t *row_ptr = IMAGE_COMPUTE_GRAYSCALE_PIXEL_ROW_PTR(arg->img, fast_floorf((y + y_offset) * scale_inv) + arg->roi->y);
                int row = input_width * y;
                for (int x = 0, xx = input_width; x < xx; x++) {
                    int pixel = IMAGE_GET_GRAYSCALE_PIXEL_FAST(row_ptr, fast_floorf((x + x_offset) * scale_inv) + arg->roi->x);
                    int index = row + x;
                    switch (input_channels) {
                        case 1: {
							((float *) model_input)[index] = pixel * fscale - offset;
                            break;
                        }
                        case 3: {
                            int index_3 = index * 3;
                            pixel = COLOR_GRAYSCALE_TO_RGB565(pixel);
							((float *) model_input)[index_3 + 0] = COLOR_RGB565_TO_R8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 1] = COLOR_RGB565_TO_G8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 2] = COLOR_RGB565_TO_B8(pixel) * fscale - offset;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
            }
            break;
        }
        case IMAGE_BPP_RGB565: {
            for (int y = 0, yy = input_height; y < yy; y++) {
                uint16_t *row_ptr = IMAGE_COMPUTE_RGB565_PIXEL_ROW_PTR(arg->img, fast_floorf((y + y_offset) * scale_inv) + arg->roi->y);
                int row = input_width * y;
                for (int x = 0, xx = input_width; x < xx; x++) {
                    int pixel = IMAGE_GET_RGB565_PIXEL_FAST(row_ptr, fast_floorf((x + x_offset) * scale_inv) + arg->roi->x);
                    int index = row + x;
                    switch (input_channels) {
                        case 1: {
							((float *) model_input)[index] = COLOR_RGB565_TO_GRAYSCALE(pixel) * fscale - offset;
                            break;
                        }
                        case 3: {
                            int index_3 = index * 3;
							((float *) model_input)[index_3 + 0] = COLOR_RGB565_TO_R8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 1] = COLOR_RGB565_TO_G8(pixel) * fscale - offset;
							((float *) model_input)[index_3 + 2] = COLOR_RGB565_TO_B8(pixel) * fscale - offset;
                            break;
                        }
                        default: {
                            break;
                        }
                    }
                }
            }
            break;
        }
        default: {
            break;
        }
    }

}

typedef struct py_glow_classify_output_data_callback_data {
    mp_obj_t out;
} py_glow_classify_output_data_callback_data_t;

STATIC void py_glow_classify_output_data_callback(void *callback_data,
                                                void *model_output,
                                                const unsigned int output_height,
                                                const unsigned int output_width,
                                                const unsigned int output_channels)
{
    py_glow_classify_output_data_callback_data_t *arg = (py_glow_classify_output_data_callback_data_t *) callback_data;

    PY_ASSERT_TRUE_MSG(output_height == 1, "Expected model output height to be 1!");
    PY_ASSERT_TRUE_MSG(output_width == 1, "Expected model output width to be 1!");

    arg->out = mp_obj_new_list(output_channels, NULL);
    for (unsigned int i = 0; i < output_channels; i++) {
		((mp_obj_list_t *) arg->out)->items[i] = mp_obj_new_float(((float *) model_output)[i]);
    }
}


STATIC mp_obj_t py_glow_classify(uint n_args, const mp_obj_t *args, mp_map_t *kw_args)
{

    fb_alloc_mark();

    py_glow_model_obj_t *arg_model = py_glow_load_alloc(args[0]);
    image_t *arg_img = py_helper_arg_to_image_mutable(args[1]);

    rectangle_t roi;
    py_helper_keyword_rectangle_roi(arg_img, n_args, args, 2, kw_args, &roi);

    float arg_min_scale = py_helper_keyword_float(n_args, args, 3, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_min_scale), 1.0f);
    PY_ASSERT_TRUE_MSG((0.0f < arg_min_scale) && (arg_min_scale <= 1.0f), "0 < min_scale <= 1");

    float arg_scale_mul = py_helper_keyword_float(n_args, args, 4, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_scale_mul), 0.5f);
    PY_ASSERT_TRUE_MSG((0.0f <= arg_scale_mul) && (arg_scale_mul < 1.0f), "0 <= scale_mul < 1");

    float arg_x_overlap = py_helper_keyword_float(n_args, args, 5, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_x_overlap), 0.0f);
    PY_ASSERT_TRUE_MSG(((0.0f <= arg_x_overlap) && (arg_x_overlap < 1.0f)) || (arg_x_overlap == -1.0f), "0 <= x_overlap < 1");

    float arg_y_overlap = py_helper_keyword_float(n_args, args, 6, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_y_overlap), 0.0f);
    PY_ASSERT_TRUE_MSG(((0.0f <= arg_y_overlap) && (arg_y_overlap < 1.0f)) || (arg_y_overlap == -1.0f), "0 <= y_overlap < 1");
	
	int offset = py_helper_keyword_int(n_args, args, 7, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_offset), 128);	
	int fscale = py_helper_keyword_int(n_args, args, 8, kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_scale), 128);

	glow_engine_t *engine = arg_model->engine;
    uint8_t *CONST = arg_model->model_data + engine->weights_offset;
	uint8_t *MUTABLE = fb_alloc(engine->mut_mem_size, FB_ALLOC_PREFER_SIZE);
	uint8_t *ACT = fb_alloc(engine->act_mem_size, FB_ALLOC_PREFER_SIZE);

    mp_obj_t objects_list = mp_obj_new_list(0, NULL);

    for (float scale = 1.0f; scale >= arg_min_scale; scale *= arg_scale_mul) {
        // Either provide a subtle offset to center multiple detection windows or center the only detection window.
        for (int y = roi.y + ((arg_y_overlap != -1.0f) ? (fmodf(roi.h, (roi.h * scale)) / 2.0f) : ((roi.h - (roi.h * scale)) / 2.0f));
            // Finish when the detection window is outside of the ROI.
            (y + (roi.h * scale)) <= (roi.y + roi.h);
            // Step by an overlap amount accounting for scale or just terminate after one iteration.
            y += ((arg_y_overlap != -1.0f) ? (roi.h * scale * (1.0f - arg_y_overlap)) : roi.h)) {
            // Either provide a subtle offset to center multiple detection windows or center the only detection window.
            for (int x = roi.x + ((arg_x_overlap != -1.0f) ? (fmodf(roi.w, (roi.w * scale)) / 2.0f) : ((roi.w - (roi.w * scale)) / 2.0f));
                // Finish when the detection window is outside of the ROI.
                (x + (roi.w * scale)) <= (roi.x + roi.w);
                // Step by an overlap amount accounting for scale or just terminate after one iteration.
                x += ((arg_x_overlap != -1.0f) ? (roi.w * scale * (1.0f - arg_x_overlap)) : roi.w)) {

                rectangle_t new_roi;
                rectangle_init(&new_roi, x, y, roi.w * scale, roi.h * scale);

                if (rectangle_overlap(&roi, &new_roi)) { // Check if new_roi is null...

                    py_glow_input_data_callback_data_t py_glow_input_data_callback_data;
                    py_glow_input_data_callback_data.img = arg_img;
                    py_glow_input_data_callback_data.roi = &new_roi;
					py_glow_input_data_callback_data.offset = offset;
					py_glow_input_data_callback_data.scale = fscale;
					
					void* model_input = MUTABLE + engine->input_offset;
					py_glow_input_data_callback(&py_glow_input_data_callback_data, 
											    model_input,
												arg_model->height,
												arg_model->width,
												arg_model->channels);
					engine->entry(CONST, MUTABLE, ACT);
					
					void* model_output = MUTABLE + engine->output_offset;
                    py_glow_classify_output_data_callback_data_t py_glow_classify_output_data_callback_data;
					py_glow_classify_output_data_callback(&py_glow_classify_output_data_callback_data,
														   model_output,
														   arg_model->out_height,
														   arg_model->out_width,
														   arg_model->out_channels
												           );

                    py_glow_classification_obj_t *o = m_new_obj(py_glow_classification_obj_t);
                    o->base.type = &py_glow_classification_type;
                    o->x = mp_obj_new_int(new_roi.x);
                    o->y = mp_obj_new_int(new_roi.y);
                    o->w = mp_obj_new_int(new_roi.w);
                    o->h = mp_obj_new_int(new_roi.h);
                    o->output = py_glow_classify_output_data_callback_data.out;
                    mp_obj_list_append(objects_list, o);
                }
            }
        }
    }

    fb_alloc_free_till_mark();

    return objects_list;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(py_glow_classify_obj, 2, py_glow_classify);

mp_obj_t py_glow_len(mp_obj_t self_in) { return mp_obj_new_int(((py_glow_model_obj_t *) self_in)->model_data_len); }
mp_obj_t py_glow_height(mp_obj_t self_in) { return mp_obj_new_int(((py_glow_model_obj_t *) self_in)->height); }
mp_obj_t py_glow_width(mp_obj_t self_in) { return mp_obj_new_int(((py_glow_model_obj_t *) self_in)->width); }
mp_obj_t py_glow_channels(mp_obj_t self_in) { return mp_obj_new_int(((py_glow_model_obj_t *) self_in)->channels); }

STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_len_obj, py_glow_len);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_height_obj, py_glow_height);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_width_obj, py_glow_width);
STATIC MP_DEFINE_CONST_FUN_OBJ_1(py_glow_channels_obj, py_glow_channels);

STATIC const mp_rom_map_elem_t locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_len), MP_ROM_PTR(&py_glow_len_obj) },
    { MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&py_glow_height_obj) },
    { MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&py_glow_width_obj) },
    { MP_ROM_QSTR(MP_QSTR_channels), MP_ROM_PTR(&py_glow_channels_obj) },
    { MP_ROM_QSTR(MP_QSTR_classify), MP_ROM_PTR(&py_glow_classify_obj) },
};

STATIC MP_DEFINE_CONST_DICT(locals_dict, locals_dict_table);

STATIC const mp_obj_type_t py_glow_model_type = {
    { &mp_type_type },
    .name  = MP_QSTR_glow_model,
    .print = py_glow_model_print,
    .locals_dict = (mp_obj_t) &locals_dict
};

STATIC const mp_rom_map_elem_t globals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_glow) },
    { MP_ROM_QSTR(MP_QSTR_load),            MP_ROM_PTR(&py_glow_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_free_from_fb),    MP_ROM_PTR(&py_glow_free_from_fb_obj) },
    { MP_ROM_QSTR(MP_QSTR_classify),        MP_ROM_PTR(&py_glow_classify_obj) },
};

STATIC MP_DEFINE_CONST_DICT(globals_dict, globals_dict_table);

const mp_obj_module_t glow_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_t) &globals_dict
};