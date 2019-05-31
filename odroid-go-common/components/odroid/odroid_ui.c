#pragma GCC optimize ("O3")

#include "odroid_ui.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"
#include <string.h>
#include "font8x8_basic.h"

extern bool DoLoadState(const char* pathName);
extern bool DoSaveState(const char* pathName);

extern bool scaling_enabled;

bool config_speedup = false;

static bool short_cut_menu_open = false;

static uint16_t *framebuffer = NULL;

static bool quicksave_done = false;

// const char* SD_TMP_PATH = "/sd/odroid/tmp";

const char* SD_TMP_PATH_SAVE = "/sd/odroid/data/.quicksav.dat";

#define color_default 0x632c
#define color_selected 0xffff
    
char buf[42];

int exec_menu();

void clean_draw_buffer() {
	int size = 320 * 8 * sizeof(uint16_t);
	memset(framebuffer, 0, size);
}

void prepare() {
	if (framebuffer) return;
	heap_caps_print_heap_info(MALLOC_CAP_8BIT);
	printf("myui_test! SETUP buffer\n");
	int size = 320 * 8 * sizeof(uint16_t);
    //uint16_t *framebuffer = (uint16_t *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
    framebuffer = (uint16_t *)malloc(size);
	if (!framebuffer) abort();
	clean_draw_buffer();
	heap_caps_print_heap_info(MALLOC_CAP_8BIT);
}

void renderToStdout(char *bitmap) {
    int x,y;
    int set;
    for (x=0; x < 8; x++) {
        for (y=0; y < 8; y++) {
            set = bitmap[x] & 1 << y;
            printf("%c", set ? 'X' : ' ');
        }
        printf("\n");
    }
}

void renderToFrameBuffer(int xo, int yo, char *bitmap, uint16_t color) {
    int x,y;
    int set;
    for (x=0; x < 8; x++) {
        for (y=0; y < 8; y++) {
            // color++;
            set = bitmap[x] & 1 << y;
            // int offset = xo + x + ((yo + y) * 320);
            int offset = xo + y + ((yo + x) * 320);
            framebuffer[offset] = set?color:0; 
        }
    }
}

void render(int offset_x, int offset_y, char *text, uint16_t color) {
	int len = strlen(text);
    int x, y;
    x = offset_x * 8;
    y = offset_y * 8;
	for (int i = 0; i < len; i++) {
	   unsigned char c = text[i];
	   renderToFrameBuffer(x, y, font8x8_basic[c], color);
	   x+=8;
	}
}

void draw_line(char *text) {
	render(0,0,text, color_selected);
	ili9341_write_frame_rectangleLE(0, 0, 320, 8, framebuffer);
}

void draw_empty_line() {
	char tmp[8];
	clean_draw_buffer();
	sprintf(tmp, " ");
	draw_line(tmp);
}

void myui_test() {
    int last_key = -1;
	int start_key = ODROID_INPUT_VOLUME;
	bool shortcut_key = false;
	odroid_gamepad_state lastJoysticState;

	prepare();
	clean_draw_buffer();
	odroid_input_gamepad_read(&lastJoysticState);
	
	//draw_empty_line();
    //draw_line("Press...");
    
	while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (!joystick.values[start_key]) {
        		last_key = start_key;
        		break;
        }
        if (last_key >=0) {
        		if (!joystick.values[last_key]) {
        			draw_line("");
        			last_key = -1;
        		}
        } else {
	        if (!lastJoysticState.values[ODROID_INPUT_UP] && joystick.values[ODROID_INPUT_UP]) {
	        		shortcut_key = true;
	        		short_cut_menu_open = true;
	        		// return;
	        		last_key = ODROID_INPUT_UP;
	        		odroid_audio_volume_change();
	        		sprintf(buf, "Volume changed to: %d", odroid_audio_volume_get()); 
	        		draw_line(buf);
	        }
	        if (!lastJoysticState.values[ODROID_INPUT_B] && joystick.values[ODROID_INPUT_B]) {
		        shortcut_key = true;
	        		last_key = ODROID_INPUT_B;
	        		sprintf(buf, "SAVE");
	        		draw_line(buf);
	        		if (DoSaveState(SD_TMP_PATH_SAVE)) {
        				sprintf(buf, "SAVE: Ok");
	        			draw_line(buf);
	        			quicksave_done = true;
        			} else {
	        			sprintf(buf, "SAVE: Error");
	        			draw_line(buf);
        			}
	        }
	        if (!lastJoysticState.values[ODROID_INPUT_A] && joystick.values[ODROID_INPUT_A]) {
		        shortcut_key = true;
	        		last_key = ODROID_INPUT_A;
	        		sprintf(buf, "LOAD");
	        		draw_line(buf);
	        		if (!quicksave_done) {
	        			sprintf(buf, "LOAD: no quicksave");
	        			draw_line(buf);
	        		} else if (DoLoadState(SD_TMP_PATH_SAVE)) {
        				sprintf(buf, "LOAD: Ok");
	        			draw_line(buf);
	        			quicksave_done = true;
        			} else {
	        			sprintf(buf, "LOAD: Error");
	        			draw_line(buf);
        			}
	        }
	    }
        lastJoysticState = joystick;
    }
    if (!shortcut_key) {
		last_key = exec_menu();
	}
    
    while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (!joystick.values[last_key]) {
        		break;
        }
    }
    draw_empty_line();
    /*if (framebuffer) {
    		free(framebuffer);
   		// heap_caps_free(framebuffer);
   		framebuffer = NULL;
   	}*/
    printf("myui_test! Continue\n");
}

int exec_menu() {
	// renderToStdout(font8x8_basic['A']);
    printf("myui_test\n");
    draw_empty_line();
    int selected = 0;
	int max = 3;
	int last_key = -1;
	
	ili9341_write_frame_gb(NULL, true);
	int counter = 0;
	while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        bool toggle = false;
        if (last_key >= 0) {
        		if (!joystick.values[last_key]) {
        			last_key = -1;
        		}
        } else {
	        if (joystick.values[ODROID_INPUT_VOLUME]) {
	            last_key = ODROID_INPUT_VOLUME;
	        		break;
	        }
	        if (joystick.values[ODROID_INPUT_LEFT]) {
	        		last_key = ODROID_INPUT_LEFT;
	        		selected--;
	        		if (selected<0) selected = max - 1;
	        }
	        if (joystick.values[ODROID_INPUT_RIGHT]) {
	        		last_key = ODROID_INPUT_RIGHT;
	        		selected++;
	        		if (selected>=max) selected = 0;
	        }
	        if (joystick.values[ODROID_INPUT_A]) {
	        		last_key = ODROID_INPUT_A;
	        		toggle = true;
	        }
        }
        
        if (toggle) {
        		switch(selected) {
        		case 0:
        			config_speedup = !config_speedup;
        		break;
        		case 1:
        			odroid_audio_volume_change();
        		break;
        		case 2:
        			scaling_enabled = !scaling_enabled;
        		break;
        		}
        }
        
        counter++;
        int x = 0;
        int y = 0;
        int entry = 0;
        uint16_t color;
        {
        		color = selected==entry?(counter%1024)&color_selected:color_default;
        		sprintf(buf, "speedup: %d", config_speedup);
			render(x, y, buf, color);
			entry++;
			x += strlen(buf)+1;
        }
        {
        		color = selected==entry?(counter%1024)&color_selected:color_default;
        		int vol = odroid_audio_volume_get();
        		sprintf(buf, "vol: %d", vol);
			render(x, y, buf, color);
			entry++;
			x += strlen(buf)+1;
        }
        {
        		color = selected==entry?(counter%1024)&color_selected:color_default;
        		sprintf(buf, "scale: %d", scaling_enabled);
			render(x, y, buf, color);
			entry++;
        }
        ili9341_write_frame_rectangleLE(0, 0, 320, 8, framebuffer);
    }
   return last_key;
}

void my_odroid_debug_enter_loop() {
	printf("LOOP\n");
	odroid_settings_Volume_set(ODROID_VOLUME_LEVEL1);
	heap_caps_print_heap_info(MALLOC_CAP_8BIT);
}

void my_odroid_debug_start() {
	heap_caps_print_heap_info(MALLOC_CAP_8BIT);
}
/*
void myui_header() {
	if (!framebuffer) return;
	
	// sprintf(buf, "MEM:0x%x, FPS:%2.2f, BAT:%d [%d]\n", esp_get_free_heap_size(), fps, battery_state.millivolts, battery_state.percentage);
	sprintf(buf, "FPS: %2.2f", fps);
	render(0, 0, buf, color_default);
	ili9341_write_frame_rectangleLE(0, 0, 320, 8, framebuffer);
}
*/