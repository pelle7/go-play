#ifndef ODROID_DISPLAY_EMU_IMPL

typedef struct __attribute__((__packed__)) {
    short left;
    short width;
    short repeat;
} odroid_scanline;

void odroid_display_reset_scale(int width, int height);
void odroid_display_set_scale(int width, int height, float aspect);
void ili9341_write_frame_8bit(uint8_t* buffer, odroid_scanline* diff, int width, int height, int stride, uint8_t pixel_mask, uint16_t* palette);

void odroid_buffer_diff(uint8_t *buffer,
                        uint8_t *old_buffer,
                        uint16_t *palette,
                        uint16_t *old_palette,
                        int width, int height, int stride,
                        uint8_t pixel_mask,
                        uint8_t palette_shift_mask,
                        odroid_scanline *out_diff);
void odroid_buffer_diff_interlaced(uint8_t *buffer,
                                   uint8_t *old_buffer,
                                   uint16_t *palette,
                                   uint16_t *old_palette,
                                   int width, int height, int stride,
                                   uint8_t pixel_mask,
                                   uint8_t palette_shift_mask,
                                   int field,
                                   odroid_scanline *out_diff,
                                   odroid_scanline *old_diff);

int odroid_buffer_diff_count(odroid_scanline *diff, int height);

#else

// The number of pixels that need to be updated to use interrupt-based updates
// instead of polling.
#define POLLING_PIXEL_THRESHOLD (LINE_BUFFER_SIZE)

// At a certain point, it's quicker to just do a single transfer for the whole
// screen than try to break it down into partial updates
#define PARTIAL_UPDATE_THRESHOLD (160*144)

static void
write_rect(uint8_t *buffer, uint16_t *palette,
           int origin_x, int origin_y,
           int left, int top, int width, int height,
           int bufferIndex, int stride, uint8_t pixel_mask,
           int x_inc, int y_inc)
{
    int actual_left, actual_width, actual_top, actual_height, ix_acc, iy_acc;

#if 1
    actual_left = ((SCREEN_WIDTH * left) + (x_inc - 1)) / x_inc;
    actual_top = ((SCREEN_HEIGHT * top) + (y_inc - 1)) / y_inc;
    int actual_right = ((SCREEN_WIDTH * (left + width)) + (x_inc - 1)) / x_inc;
    int actual_bottom = ((SCREEN_HEIGHT * (top + height)) + (y_inc - 1)) / y_inc;
    actual_width = actual_right - actual_left;
    actual_height = actual_bottom - actual_top;
    ix_acc = (x_inc * actual_left) % SCREEN_WIDTH;
    iy_acc = (y_inc * actual_top) % SCREEN_HEIGHT;
#else
    // Leaving these here for reference, the above equations should produce
    // equivalent results.
    actual_left = actual_width = ix_acc = 0;
    for (int x = 0, x_acc = 0, ax = 0; x < left + width; ++ax) {
        x_acc += x_inc;
        while (x_acc >= SCREEN_WIDTH) {
            x_acc -= SCREEN_WIDTH;
            ++x;

            if (x == left) {
                ix_acc = x_acc;
                actual_left = ax + 1;
            }
            if (x == left + width) {
                actual_width = (ax - actual_left) + 1;
            }
        }
    }

    actual_top = actual_height = iy_acc = 0;
    for (int y = 0, y_acc = 0, ay = 0; y < top + height; ++ay) {
        y_acc += y_inc;
        while (y_acc >= SCREEN_HEIGHT) {
            y_acc -= SCREEN_HEIGHT;
            ++y;

            if (y == top) {
                iy_acc = y_acc;
                actual_top = ay + 1;
            }
            if (y == top + height) {
                actual_height = (ay - actual_top) + 1;
            }
        }
    }
#endif

    if (actual_width == 0 || actual_height == 0) {
        return;
    }

    send_reset_drawing(origin_x + actual_left, origin_y + actual_top,
                       actual_width, actual_height);

    int line_count = LINE_BUFFER_SIZE / actual_width;
    for (int y = 0, y_acc = iy_acc; y < height;)
    {
        int line_buffer_index = 0;
        uint16_t* line_buffer = line_buffer_get();

        int lines_to_copy = 0;
        for (; (lines_to_copy < line_count) && (y < height); ++lines_to_copy)
        {
            for (int x = 0, x_acc = ix_acc; x < width;)
            {
                line_buffer[line_buffer_index++] =
                  palette[buffer[bufferIndex + x] & pixel_mask];

                x_acc += x_inc;
                while (x_acc >= SCREEN_WIDTH) {
                    ++x;
                    x_acc -= SCREEN_WIDTH;
                }
            }

            y_acc += y_inc;
            while (y_acc >= SCREEN_HEIGHT) {
                ++y;
                bufferIndex += stride;
                y_acc -= SCREEN_HEIGHT;
            }
        }

        send_continue_line(line_buffer, actual_width, lines_to_copy);
    }
}

static int x_inc = SCREEN_WIDTH;
static int y_inc = SCREEN_HEIGHT;
static int x_origin = 0;
static int y_origin = 0;
static float x_scale = 1.f;
static float y_scale = 1.f;

void
odroid_display_reset_scale(int width, int height)
{
    x_inc = SCREEN_WIDTH;
    y_inc = SCREEN_HEIGHT;
    x_origin = (SCREEN_WIDTH - width) / 2;
    y_origin = (SCREEN_HEIGHT - height) / 2;
    x_scale = y_scale = 1.f;
}

void
odroid_display_set_scale(int width, int height, float aspect)
{
    float buffer_aspect = ((width * aspect) / (float)height);
    float screen_aspect = SCREEN_WIDTH / (float)SCREEN_HEIGHT;

    if (buffer_aspect < screen_aspect) {
        y_scale = SCREEN_HEIGHT / (float)height;
        x_scale = y_scale * aspect;
    } else {
        x_scale = SCREEN_WIDTH / (float)width;
        y_scale = x_scale / aspect;
    }

    x_inc = SCREEN_WIDTH / x_scale;
    y_inc = SCREEN_HEIGHT / y_scale;
    x_origin = (SCREEN_WIDTH - (width * x_scale)) / 2.f;
    y_origin = (SCREEN_HEIGHT - (height * y_scale)) / 2.f;

    printf("%dx%d@%.3f x_inc:%d y_inc:%d x_scale:%.3f y_scale:%.3f x_origin:%d y_origin:%d\n",
           width, height, aspect, x_inc, y_inc, x_scale, y_scale, x_origin, y_origin);
}

void
ili9341_write_frame_8bit(uint8_t* buffer, odroid_scanline *diff,
                         int width, int height, int stride,
                         uint8_t pixel_mask, uint16_t* palette)
{
    if (!buffer) {
        ili9341_clear(0);
        return;
    }

    odroid_display_lock();

    spi_device_acquire_bus(spi, portMAX_DELAY);

#if 0
    if (diff) {
        int n_pixels = odroid_buffer_diff_count(diff, height);
        if (n_pixels * scale > PARTIAL_UPDATE_THRESHOLD) {
            diff = NULL;
        }
    }
#endif

    bool need_interrupt_updates = false;
    int left = 0;
    int line_width = width;
    int repeat = 0;

#if 0
    // Make all updates interrupt updates
    poll_threshold = 0;
#elif 0
    // Make all updates polling updates
    poll_threshold = INT_MAX;
#endif

    // Do polling updates first
    use_polling = true;
    for (int y = 0, i = 0; y < height; ++y, i += stride, --repeat)
    {
        if (repeat > 0) continue;

        if (diff) {
            left = diff[y].left;
            line_width = diff[y].width;
            repeat = diff[y].repeat;
        } else {
            repeat = height;
        }

        if (line_width > 0) {
            int n_pixels = (line_width * x_scale) * (repeat * y_scale);
            if (n_pixels < POLLING_PIXEL_THRESHOLD) {
                write_rect(buffer, palette, x_origin, y_origin,
                           left, y, line_width, repeat, i + left, stride,
                           pixel_mask, x_inc, y_inc);
            } else {
                need_interrupt_updates = true;
            }
        }
    }
    use_polling = false;

    // Use interrupt updates for larger areas
    if (need_interrupt_updates) {
        repeat = 0;
        for (int y = 0, i = 0; y < height; ++y, i += stride, --repeat)
        {
            if (repeat > 0) continue;

            if (diff) {
                left = diff[y].left;
                line_width = diff[y].width;
                repeat = diff[y].repeat;
            } else {
                repeat = height;
            }

            if (line_width) {
                int n_pixels = (line_width * x_scale) * (repeat * y_scale);
                if (n_pixels >= POLLING_PIXEL_THRESHOLD) {
                    write_rect(buffer, palette, x_origin, y_origin,
                               left, y, line_width, repeat, i + left, stride,
                               pixel_mask, x_inc, y_inc);
                }
            }
        }
    }

    spi_device_release_bus(spi);

    odroid_display_unlock();
}

static inline bool
pixel_diff(uint8_t *buffer1, uint8_t *buffer2,
           uint16_t *palette1, uint16_t *palette2,
           uint8_t pixel_mask, uint8_t palette_shift_mask,
           int idx)
{
    uint8_t p1 = (buffer1[idx] & pixel_mask);
    uint8_t p2 = (buffer2[idx] & pixel_mask);
    if (!palette1)
        return p1 != p2;

    if (palette_shift_mask) {
        if (buffer1[idx] & palette_shift_mask) p1 += (pixel_mask + 1);
        if (buffer2[idx] & palette_shift_mask) p2 += (pixel_mask + 1);
    }

    return palette1[p1] != palette2[p2];
}

static void IRAM_ATTR
odroid_buffer_diff_internal(uint8_t *buffer, uint8_t *old_buffer,
                   uint16_t *palette, uint16_t *old_palette,
                   int width, int height, int stride, uint8_t pixel_mask,
                   uint8_t palette_shift_mask,
                   odroid_scanline *out_diff)
{
    if (!old_buffer) {
        for (int y = 0; y < height; ++y) {
            out_diff[y].left = 0;
            out_diff[y].width = width;
            out_diff[y].repeat = 1;
        }
    } else {
        int i = 0;
        uint32_t pixel_mask32 = (pixel_mask << 24) | (pixel_mask << 16) |
                                (pixel_mask <<8) | pixel_mask;
        for (int y = 0; y < height; ++y, i += stride) {
            out_diff[y].left = width;
            out_diff[y].width = 0;
            out_diff[y].repeat = 1;

            if (!palette) {
                // This is only accurate to 4 pixels of course, but much faster
                uint32_t *buffer32 = &buffer[i];
                uint32_t *old_buffer32 = &old_buffer[i];
                for (int x = 0; x < width>>2; ++x) {
                    if ((buffer32[x] & pixel_mask32) !=
                        (old_buffer32[x] & pixel_mask32))
                    {
                        out_diff[y].left = x << 2;
                        for (x = (width-1)>>2; x >= 0; --x) {
                            if ((buffer32[x] & pixel_mask32) !=
                                (old_buffer32[x] & pixel_mask32)) {
                                out_diff[y].width = (((x + 1)<<2) - out_diff[y].left);
                                break;
                            }
                        }
                        break;
                    }
                }
            } else {
                for (int x = 0, idx = i; x < width; ++x, ++idx) {
                    if (!pixel_diff(buffer, old_buffer, palette, old_palette,
                                    pixel_mask, palette_shift_mask, idx)) {
                        continue;
                    }
                    out_diff[y].left = x;

                    for (x = width - 1, idx = i + (width - 1);
                         x >= 0; --x, --idx)
                    {
                        if (!pixel_diff(buffer, old_buffer, palette, old_palette,
                                        pixel_mask, palette_shift_mask, idx)) {
                            continue;
                        }
                        out_diff[y].width = (x - out_diff[y].left) + 1;
                        break;
                    }
                    break;
                }
            }
        }
    }
}

static void IRAM_ATTR
odroid_buffer_diff_optimize(odroid_scanline *diff, int height)
{
    // Run through and count how many lines each particular run has
    // so that we can optimise and use write_continue and save on SPI
    // bandwidth.
    // Because of the bandwidth required to setup the page/column
    // address, etc., it can actually cost more to run setup than just
    // transfer the extra pixels.
    for (int y = height - 1; y > 0; --y) {
        int left_diff = abs(diff[y].left - diff[y-1].left);
        if (left_diff > 8) continue;

        int right = diff[y].left + diff[y].width;
        int right_prev = diff[y-1].left + diff[y-1].width;
        int right_diff = abs(right - right_prev);
        if (right_diff > 8) continue;

        if (diff[y].left < diff[y-1].left)
          diff[y-1].left = diff[y].left;
        diff[y-1].width = (right > right_prev) ?
          right - diff[y-1].left : right_prev - diff[y-1].left;
        diff[y-1].repeat = diff[y].repeat + 1;
    }
}

static inline bool
palette_diff(uint16_t *palette1, uint16_t *palette2, int size)
{
    for (int i = 0; i < size; ++i) {
        if (palette1[i] != palette2[i]) {
            return true;
        }
    }
    return false;
}

void IRAM_ATTR
odroid_buffer_diff(uint8_t *buffer, uint8_t *old_buffer,
                   uint16_t *palette, uint16_t *old_palette,
                   int width, int height, int stride, uint8_t pixel_mask,
                   uint8_t palette_shift_mask,
                   odroid_scanline *out_diff)
{
    if (palette &&
        !palette_diff(palette, old_palette, pixel_mask + 1))
    {
        // This may cause over-diffing the frame after a palette change on an
        // interlaced frame, but I think we can deal with that.
        pixel_mask |= palette_shift_mask;
        palette_shift_mask = 0;
        palette = NULL;
    }

    odroid_buffer_diff_internal(buffer, old_buffer, palette, old_palette,
                                width, height, stride, pixel_mask,
                                palette_shift_mask, out_diff);
    odroid_buffer_diff_optimize(out_diff, height);
}

void IRAM_ATTR
odroid_buffer_diff_interlaced(uint8_t *buffer, uint8_t *old_buffer,
                              uint16_t *palette, uint16_t *old_palette,
                              int width, int height, int stride,
                              uint8_t pixel_mask, uint8_t palette_shift_mask,
                              int field,
                              odroid_scanline *out_diff,
                              odroid_scanline *old_diff)
{
    bool palette_changed = false;

    // If the palette might've changed then we need to just copy the whole
    // old palette and scanline.
    if (old_buffer) {
        if (palette_shift_mask) {
            palette_changed = palette_diff(palette, old_palette, pixel_mask + 1);
        }

        if (palette_changed) {
            memcpy(&palette[(pixel_mask+1)], old_palette,
                   (pixel_mask + 1) * sizeof(uint16_t));

            for (int y = 1 - field; y < height; y += 2) {
                int idx = y * stride;
                for (int x = 0; x < width; ++x) {
                    buffer[idx+x] = (old_buffer[idx+x] & pixel_mask) |
                                    palette_shift_mask;
                }
            }
        } else {
            // If the palette didn't change then no pixels in this frame will have
            // the palette_shift bit set, so this may cause over-diffing after
            // palette changes but is otherwise ok.
            pixel_mask |= palette_shift_mask;
            palette_shift_mask = 0;
            palette = NULL;
        }
    }

    odroid_buffer_diff_internal(buffer + (field * stride),
                                old_buffer ? old_buffer + (field * stride) : NULL,
                                palette, old_palette,
                                width, height / 2,
                                stride * 2, pixel_mask,
                                palette_shift_mask,
                                out_diff);

    for (int y = height - 1; y >= 0; --y) {
        if ((y % 2) ^ field) {
            out_diff[y].width = 0;
            out_diff[y].repeat = 1;
            if (!palette_changed && old_buffer) {
                int idx = (y * stride) + old_diff[y].left;
                memcpy(&buffer[idx], &old_buffer[idx], old_diff[y].width);
            }
        } else {
            out_diff[y] = out_diff[y/2];
        }
    }
}

int IRAM_ATTR
odroid_buffer_diff_count(odroid_scanline *diff, int height)
{
    int n_pixels = 0;
    for (int y = 0; y < height;) {
        n_pixels += diff[y].width * diff[y].repeat;
        y += diff[y].repeat;
    }
    return n_pixels;
}

#endif
