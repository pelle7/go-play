#ifndef ODROID_DISPLAY_EMU_IMPL

void ili9341_write_frame_gb(uint16_t* buffer, int scale);

#else

 // GB
#define GAMEBOY_WIDTH (160)
#define GAMEBOY_HEIGHT (144)

void ili9341_write_frame_gb(uint16_t* buffer, int scale)
{
    short x, y;

    odroid_display_lock();

    //xTaskToNotify = xTaskGetCurrentTaskHandle();

    if (buffer == NULL)
    {
        // clear the buffer
        for (int i = 0; i < LINE_BUFFERS; ++i)
        {
            memset(line[i], 0, 320 * sizeof(uint16_t) * LINE_COUNT);
        }

        // clear the screen
        send_reset_drawing(0, 0, 320, 240);

        for (y = 0; y < 240; y += LINE_COUNT)
        {
            uint16_t* line_buffer = line_buffer_get();
            send_continue_line(line_buffer, 320, LINE_COUNT);
        }
    }
    else
    {
        uint16_t* framePtr = buffer;

        if (scale)
        {
            // NOTE: LINE_COUNT must be 3 or greater
            const short outputWidth = 265;
            const short outputHeight = 240;

            send_reset_drawing(26, 0, outputWidth, outputHeight);

            for (y = 0; y < GAMEBOY_HEIGHT; y += 3)
            {
                uint16_t* line_buffer = line_buffer_get();

                for (int i = 0; i < 3; ++i)
                {
                    // skip middle vertical line
                    int index = i * outputWidth * 2;
                    int bufferIndex = ((y + i) * GAMEBOY_WIDTH);

                    for (x = 0; x < GAMEBOY_WIDTH; x += 3)
                    {
                        uint16_t a = framePtr[bufferIndex++];
                        uint16_t b;
                        uint16_t c;

                        if (x < GAMEBOY_WIDTH - 1)
                        {
                            b = framePtr[bufferIndex++];
                            c = framePtr[bufferIndex++];
                        }
                        else
                        {
                            b = framePtr[bufferIndex++];
                            c = 0;
                        }

                        uint16_t mid1 = Blend(a, b);
                        uint16_t mid2 = Blend(b, c);

                        line_buffer[index++] = ((a >> 8) | ((a) << 8));
                        line_buffer[index++] = ((mid1 >> 8) | ((mid1) << 8));
                        line_buffer[index++] = ((b >> 8) | ((b) << 8));
                        line_buffer[index++] = ((mid2 >> 8) | ((mid2) << 8));
                        line_buffer[index++] = ((c >> 8) | ((c ) << 8));
                    }
                }

                // Blend top and bottom lines into middle
                short sourceA = 0;
                short sourceB = outputWidth * 2;
                short sourceC = sourceB + (outputWidth * 2);

                short output1 = outputWidth;
                short output2 = output1 + (outputWidth * 2);

                for (short j = 0; j < outputWidth; ++j)
                {
                    uint16_t a = line_buffer[sourceA++];
                    a = ((a >> 8) | ((a) << 8));

                    uint16_t b = line_buffer[sourceB++];
                    b = ((b >> 8) | ((b) << 8));

                    uint16_t c = line_buffer[sourceC++];
                    c = ((c >> 8) | ((c) << 8));

                    uint16_t mid = Blend(a, b);
                    mid = ((mid >> 8) | ((mid) << 8));

                    line_buffer[output1++] = mid;

                    uint16_t mid2 = Blend(b, c);
                    mid2 = ((mid2 >> 8) | ((mid2) << 8));

                    line_buffer[output2++] = mid2;
                }

                // send the data
                send_continue_line(line_buffer, outputWidth, 5);
            }
        }
        else
        {
            send_reset_drawing((320 / 2) - (GAMEBOY_WIDTH / 2),
                (240 / 2) - (GAMEBOY_HEIGHT / 2),
                GAMEBOY_WIDTH,
                GAMEBOY_HEIGHT);

            for (y = 0; y < GAMEBOY_HEIGHT; y += LINE_COUNT)
            {
              uint16_t* line_buffer = line_buffer_get();

              int linesWritten = 0;

              for (int i = 0; i < LINE_COUNT; ++i)
              {
                  if((y + i) >= GAMEBOY_HEIGHT) break;

                  int index = (i) * GAMEBOY_WIDTH;
                  int bufferIndex = ((y + i) * GAMEBOY_WIDTH);

                  for (x = 0; x < GAMEBOY_WIDTH; ++x)
                  {
                    uint16_t sample = framePtr[bufferIndex++];
                    line_buffer[index++] = ((sample >> 8) | ((sample & 0xff) << 8));
                  }

                  ++linesWritten;
              }

              send_continue_line(line_buffer, GAMEBOY_WIDTH, linesWritten);
            }
        }
    }

    odroid_display_unlock();
}

#endif
