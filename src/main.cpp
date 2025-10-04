// PlatformIO: framework = arduino; board = esp32-s3 DevKit (or your S3)
// Library: LVGL v9.x

#include <main.h>
#include <Arduino.h>

#include <ArduinoLog.h>

#include <lvgl.h>
#include <LovyanGFX.hpp>

#include <ESP32Encoder.h>   // https://github.com/madhephaestus/ESP32Encoder.git

ESP32Encoder rotaryEncoder;
static volatile int64_t last_raw = 0;
static int steps_remainder = 0;          // for partial steps

static const int steps_per_detent = 1;  // tune after testing
                                        //
                                        //
void encoder_init(int pinA, int pinB) {
  pinMode(pinA,INPUT_PULLUP);
  pinMode(pinB,INPUT_PULLUP);

  // ESP32Encoder::useInternalWeakPullResistors = puType::up;   // optional
  // Choose one:
  rotaryEncoder.attachHalfQuad(pinA, pinB);     // preferred for UI detents
  // enc.attachFullQuad(pinA, pinB);  // higher resolution if needed
  rotaryEncoder.clearCount();
  last_raw = 0;
}


/* static auto tick_get_cb = []() -> uint32_t {
   return esp_timer_get_time() / 1000ULL;
 };
*/

extern "C" uint32_t tick_get_cb(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}


static LGFX gfx;

const unsigned int lvBufferSize = TFT_WIDTH * TFT_HEIGHT / 10 * (LV_COLOR_DEPTH / 8);
  uint8_t lvBuffer[lvBufferSize];

static lv_indev_t *indev_encoder = nullptr;
static lv_indev_t *indev_keypad  = nullptr;

lv_obj_t *word_label;
lv_obj_t *trainer_page;

static lv_obj_t * volume_slider_label;
static lv_obj_t * wpm_slider_label;



void M32Pocket_hal_init() {
#ifndef VEXT_ON_VALUE
#define VEXT_ON_VALUE LOW
#endif

#ifdef PIN_VEXT
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, VEXT_ON_VALUE);
#endif

}

static void lvgl_encoder_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  const int64_t raw = rotaryEncoder.getCount();
  Log.verboseln("encoder read CB: %u",raw);
  const int diff_counts = (int)(raw - last_raw);
  last_raw = raw;

  int det = diff_counts + steps_remainder;
  const int step = det / steps_per_detent;
  steps_remainder = det % steps_per_detent;

  data->enc_diff = (int16_t)step;
  data->state = (digitalRead(PIN_ROT_BTN) == HIGH)
                  ? LV_INDEV_STATE_RELEASED
                  : LV_INDEV_STATE_PRESSED;
}

void my_disp_flush(lv_display_t* display, const lv_area_t* area, unsigned char* px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);

  if (gfx.getStartCount()) gfx.startWrite();

  gfx.setAddrWindow(area->x1, area->y1, w, h);
  gfx.pushPixelsDMA((uint16_t *)px_map, w * h, true);
  //gfx.endWrite();
  lv_display_flush_ready(display);
}

void main_UI(void)
{

  lv_obj_t * tabview;
  tabview = lv_tabview_create(lv_screen_active());
  //lv_tabview_set_tab_bar_size(tabview, LV_SIZE_CONTENT);
  lv_tabview_set_tab_bar_size(tabview, lv_pct(15));
  
  lv_obj_t * config_page = lv_tabview_add_tab(tabview, "Config");
  trainer_page = lv_tabview_add_tab(tabview, "Trainer");
  lv_tabview_set_active(tabview, 1, LV_ANIM_OFF); // set trainer tab active

  lv_obj_t *config_menu = lv_menu_create(config_page);
  lv_obj_set_size(config_menu, lv_obj_get_width(config_page), lv_obj_get_height(config_page));

  lv_obj_t * mainsettings_page = lv_menu_page_create(config_menu, NULL);
  lv_menu_set_page(config_menu, mainsettings_page);
  lv_obj_t * cont;

  // volume slider
  cont = lv_menu_cont_create(mainsettings_page);
  lv_obj_t * volume_slider = lv_slider_create(cont);
  //lv_slider_set_value(volume_slider, prefs.getUInt("volume",100), LV_ANIM_OFF);
  lv_obj_center(volume_slider);
//  lv_obj_add_event_cb(volume_slider, volume_slider_event_cb, LV_EVENT_RELEASED, NULL);

  lv_obj_set_style_anim_duration(volume_slider, 2000, 0);
  /*Create a label below the slider*/
  cont = lv_menu_cont_create(mainsettings_page);
  volume_slider_label = lv_label_create(cont);
  lv_label_set_text(volume_slider_label, "0%");
  lv_obj_center(volume_slider_label);

  lv_obj_align_to(volume_slider_label, volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // wpm slider
  cont = lv_menu_cont_create(mainsettings_page);
  lv_obj_t * wpm_slider = lv_slider_create(cont);
  lv_slider_set_range(wpm_slider, 5, 45);
  //lv_slider_set_value(wpm_slider, speed_wpm, LV_ANIM_OFF);

  lv_obj_center(wpm_slider);
//  lv_obj_add_event_cb(wpm_slider, wpm_slider_event_cb, LV_EVENT_RELEASED, NULL);
  cont = lv_menu_cont_create(mainsettings_page);
  wpm_slider_label = lv_label_create(cont);
  lv_label_set_text(wpm_slider_label, "0");
  lv_obj_center(wpm_slider_label);

  lv_obj_align_to(volume_slider_label, volume_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // this is used to print the prases from the morse trainer player and echo trainer success/failure
  word_label = lv_label_create(trainer_page);
  lv_label_set_text(word_label, "");
  lv_obj_set_width(word_label, LV_SIZE_CONTENT);
  lv_obj_set_height(word_label, LV_SIZE_CONTENT);
  // no idea why this crashes:
  //   static lv_style_t wordlabel_style;
  //   lv_style_init(&wordlabel_style);
  //   lv_style_set_text_font(&wordlabel_style, LV_FONT_MONTSERRAT_28);  /*Set a larger font*/
  //   lv_obj_add_style(word_label, &wordlabel_style, 0);

  // only few options instead, this isn't very large:
  lv_obj_set_style_text_font(word_label, lv_theme_get_font_large(word_label), LV_PART_MAIN);

  // floating play/pause button on the trainer tab
  lv_obj_t * play_float_btn = lv_button_create(trainer_page);
  lv_obj_set_size(play_float_btn, 50, 50);
  lv_obj_add_flag(play_float_btn, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(play_float_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -lv_obj_get_style_pad_right(word_label, LV_PART_MAIN));
  //lv_obj_add_event_cb(play_float_btn, play_btn_event_cb, LV_EVENT_ALL, NULL);
  lv_obj_set_style_radius(play_float_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_image_src(play_float_btn, LV_SYMBOL_PLAY, 0);
  lv_obj_set_style_text_font(play_float_btn, lv_theme_get_font_large(play_float_btn), 0);
}



void setup()
{
  Serial.begin(115200);
  delay(2000);
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  Log.verboseln("M32 NG - Startup");

  Log.verboseln("HAL init");
  M32Pocket_hal_init();

  Log.verboseln("Rotary encoder init");
  pinMode(PIN_ROT_BTN, INPUT_PULLUP);
  pinMode(0, INPUT_PULLUP);
  encoder_init(PIN_ROT_DT, PIN_ROT_CLK);

  Log.verboseln("LovyanGFX init");
  gfx.begin();
  gfx.setRotation(3);

  Log.verboseln("LGVL init");
  lv_init();
  lv_tick_set_cb(tick_get_cb);

  Log.verboseln("LGVL create indevs");
  indev_encoder = lv_indev_create();
  lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
  lv_indev_set_read_cb(indev_encoder, lvgl_encoder_read_cb);

  Log.verboseln("LGVL create display");
  static auto *lvDisplay = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  lv_display_set_rotation(lvDisplay, LV_DISPLAY_ROTATION_90);
  //lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(lvDisplay, my_disp_flush);
  lv_display_set_buffers(lvDisplay, lvBuffer, nullptr, lvBufferSize, LV_DISPLAY_RENDER_MODE_PARTIAL);

/*
  // Group to navigate with encoder
  lv_group_t * g = lv_group_create();
  lv_indev_set_group(indev_encoder, g);
  lv_indev_set_group(indev_keypad,  g);
*/

  /*
      lv_obj_t *label = lv_label_create( lv_scr_act() );
    lv_label_set_text( label, "Hello Arduino, I'm LVGL!" );
    lv_obj_align( label, LV_ALIGN_CENTER, 0, 0 );
    */
  Log.verboseln("LGVL main UI");
  main_UI();

}

/* ------------------------------ Loop -------------------------------- */
void loop()
{
    lv_timer_handler();
    delay(5);
}


