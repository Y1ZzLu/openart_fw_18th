
# init

import ustruct as struct

import lodepng as png
import lvgl as lv
lv.init()
lv.log_register_print_cb(lambda level,path,line,msg: print('%s(%d): %s' % (path, line, msg)))

import SDL
SDL.init()

# Register SDL display driver.

disp_buf1 = lv.disp_buf_t()
buf1_1 = bytes(480*10)
disp_buf1.init(buf1_1, None, len(buf1_1)//4)
disp_drv = lv.disp_drv_t()
disp_drv.init()
disp_drv.buffer = disp_buf1
disp_drv.flush_cb = SDL.monitor_flush
disp_drv.hor_res = 480
disp_drv.ver_res = 320
disp_drv.register()

# Regsiter SDL mouse driver

indev_drv = lv.indev_drv_t()
indev_drv.init()
indev_drv.type = lv.INDEV_TYPE.POINTER
indev_drv.read_cb = SDL.mouse_read
indev_drv.register()

# Parse PNG file header
# Taken from https://github.com/shibukawa/imagesize_py/blob/ffef30c1a4715c5acf90e8945ceb77f4a2ed2d45/imagesize.py#L63-L85

def get_png_info(decoder, src, header):
    # Only handle variable image types

    if lv.img.src_get_type(src) != lv.img.SRC.VARIABLE:
        return lv.RES.INV

    png_header = bytes(lv.img_dsc_t.cast(src).data.__dereference__(24))

    if png_header.startswith(b'\211PNG\r\n\032\n') and png_header[12:16] == b'IHDR':
        try:
            width, height = struct.unpack(">LL", png_header[16:24])
        except struct.error:
            return lv.RES.INV
    
    # Maybe this is for an older PNG version.
    
    elif png_header.startswith(b'\211PNG\r\n\032\n'):
        # Check to see if we have the right content type
        try:
            width, height = struct.unpack(">LL", png_header[8:16])
        except struct.error:
            return lv.RES.INV
    else:
        return lv.RES.INV

    header.always_zero = 0
    header.w = width
    header.h = height
    header.cf = lv.img.CF.TRUE_COLOR_ALPHA

    # print("width=%d, height=%d" % (header.w, header.h))

    return lv.RES.OK


# Read and parse PNG file

def open_png(decoder, dsc):
    img_dsc = lv.img_dsc_t.cast(dsc.src)
    png_data = img_dsc.data
    png_size = img_dsc.data_size
    png_decoded = png.C_Pointer()
    png_width = png.C_Pointer()
    png_height = png.C_Pointer()
    error = png.decode32(png_decoded, png_width, png_height, png_data, png_size);
    if error:
        return None # LV_IMG_DECODER_OPEN_FAIL
    img_size = png_width.int_val * png_height.int_val * lv.color_t.SIZE
    img_data = png_decoded.ptr_val

    # Convert png RGBA-big-endian format to lvgl ARGB-little-endian
    # Eventually - this means swapping red and blue.
    # More info on https://forumtest.littlevgl.com/t/png-decoding-why-red-and-blue-are-swapped/72

    img_view = img_data.__dereference__(img_size)
    for i in range(0, img_size, lv.color_t.SIZE):
        ch = lv.color_t.cast(img_view[i:i]).ch
        ch.red, ch.blue = ch.blue, ch.red

    dsc.img_data = img_data
    return lv.RES.OK

# Register new image decoder

decoder = lv.img.decoder_create()
decoder.info_cb = get_png_info
decoder.open_cb = open_png

# Create a screen with a draggable image

with open('lib/lv_bindings/examples/png_decoder_test.png','rb') as f:
  png_data = f.read()

png_img_dsc = lv.img_dsc_t({
    'data_size': len(png_data),
    'data': png_data 
})

scr = lv.obj()

# Create an image on the left using the decoder

lv.img.cache_set_size(2)
img1 = lv.img(scr)
img1.align(scr, lv.ALIGN.IN_LEFT_MID, -50, 0)
img1.set_src(png_img_dsc)
img1.set_drag(True)

# Create an image on the right directly without the decoder

img2 = lv.img(scr)
img2.align(scr, lv.ALIGN.IN_RIGHT_MID, 0, 0)
raw_dsc = lv.img_dsc_t()
get_png_info(None, png_img_dsc, raw_dsc.header)
dsc = lv.img_decoder_dsc_t({'src': png_img_dsc})
if open_png(None, dsc) == lv.RES.OK:
    raw_dsc.data = dsc.img_data
    raw_dsc.data_size = raw_dsc.header.w * raw_dsc.header.h * lv.color_t.SIZE
    img2.set_src(raw_dsc)
    img2.set_drag(True)

# Load the screen and display image

lv.scr_load(scr)

