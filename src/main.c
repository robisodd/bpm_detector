#include <pebble.h>
// samples_to_collect: how long to discard old taps
//   when accelerometer is at 100Hz, this is in 10s of ms.  (e.g. 300 = 3 seconds)
//   so it will average the time between taps over the past 3 seconds.
//   if this is smaller, it'll react quicker but you won't be able to measure slow bpm (like < 60bpm)
#define samples_to_collect 300
#define sensitivity_count 5
#define sensitivity_text_removal_delay 3000
#define PERSIST_VERSION_KEY 0
#define PERSIST_SENSITIVITY_KEY 1
#define CURRENT_VERSION 2

       Window *main_window;
    TextLayer *bpm_layer, *sensitivity_layer, *time_layer;
     AppTimer *sensitivity_text_timer = NULL;
         char  bpm_text[] = " 0 bpm ";
         char  time_text[20] = "";
      int16_t  offset = 1000;
      int16_t  buffer[samples_to_collect];
      int16_t  sensitivity_lookup[sensitivity_count] = {15, 30, 60, 120, 240};
      int16_t  sensitivity_index = PBL_IF_RECT_ELSE(2, 1);
      int16_t  sensitivity = 55;
         char *sensitivity_text[sensitivity_count] = {"Sensitivity:\nVery Light", "Sensitivity:\nLight", "Sensitivity:\nMedium", "Sensitivity:\nHard", "Sensitivity:\nVery Hard"};

int16_t abs16(int16_t x) {return (x ^ (x >> 15)) - (x >> 15);}  // quick 16-bit absolute value function

void accel_handler(AccelData *data, uint32_t num_samples) {
  uint32_t lastpoint = 1000, count = 0, bpm = 0;
  
  // if this is the first time this function is called, fill buffer with current data
  //  (this is to stop detecting the jump from 0 to "resting acceleromter" as a tap)
  if(offset == 1000) {
    offset = 0;
    for(uint32_t i=0; i<samples_to_collect; i++)
      buffer[i] = data[0].z >> 3;
  }
  
  // read accelerometer data and add samples to the rotating buffer
  for(uint32_t i=0; i<num_samples; i++) {
    buffer[offset] = data[i].z >> 3;             // replace oldest sample with newest (it seems accelerometer data is always evenly divisible by 8, hence the ">> 3")
    offset = (offset + 1) % samples_to_collect;  // set offset to point to what has now become the oldest sample
  }
  
  // average the time between all taps in the sample buffer
  for(int16_t i=0; i<samples_to_collect; i++) {
    if(abs16(buffer[(i+offset)%samples_to_collect] - buffer[(i+offset+1)%samples_to_collect]) > sensitivity) {// if Tap Detected (magnitude between two accelerometer measurements is more than the sensitivity threshold)
      if(lastpoint == 1000)         // if this is the first tap
        lastpoint = i;              // record timestamp as initial tap (technically this should be "(i+offset)%samples_to_collect", but we want the difference between 2 points so both cancel out)
      if((i-lastpoint) > 10) {      // if samples_between_taps is less than 1/10 sec (within 10 samples @ 100Hz), disregard tap (smooth jitters) -- limits to 600bpm
        bpm = bpm + (i-lastpoint);  // add to the total_samples_between_taps
        lastpoint = i;              // update lastpoint to be able to time the next span
        count++;                    // one more tap detected
      }
    } 
  }
  bpm = (6000 * count) / bpm;  // average = total divided by number collected (hence an average), and bpm = 60sec * 100Hz / average. so: bpm = (60s * 100Hz * number_collected) / total
  // Hmm, that statement on the line above might be confusing. Here's the breakdown:
  //   First, get the average:
  //                average_samples_between_taps (samples/tap) = total_samples_between_taps / number_of_taps  (I mean, that's what an "average" is.)
  //   Then convert from samples to milliseconds based on 100Hz accelerometer setting:
  //                average_milliseconds_between_taps (ms/tap) = ((1000 ms/sec) / 100 Hz (aka samples/sec)) * average_samples_between_taps (samples/tap)
  //   Finally, much like frequency is 1/period, or framerate is 1/time_to_draw_one_frame, calculate the beats per millisecond:
  //                taps_per_millisecond = 1 / average_milliseconds_between_taps (samples/tap)
  //   And convert taps_per_millisecond to taps_per_second:
  //                taps_per_second = (1000 ms/sec) * taps_per_millisecond
  //   And convert taps_per_millisecond to taps_per_minute:
  //                bpm (aka taps/min) = (60 sec/min) * taps_per_second
  
  
  if(bpm > 999) bpm = 999;  // bpm_text holds 3 digits max -- just in case (shouldn't be needed).  Also, since bpm is unsigned so it'll never go < 0, so no need to check for that
  
  // display the text
  // bpm = 135;  // Uncomment this line to have it always say "135 bpm", for photo ops
  snprintf(bpm_text, sizeof(bpm_text), "%d bpm", (int)bpm);
  text_layer_set_text(bpm_layer, bpm_text);
  
  // display sensitivity text
  text_layer_set_text(sensitivity_layer, sensitivity_text[sensitivity_index]);
  
  // display time
  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  char *time_format;
  
  if (clock_is_24h_style()) {
    time_format = "%H:%M";
  } else {
    time_format = "%I:%M%p";
  }
  strftime(time_text, sizeof(time_text), time_format, local);
  
  // Handle lack of non-padded hour format string for twelve hour clock.
  if (!clock_is_24h_style() && (time_text[0] == '0'))
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  
  text_layer_set_text(time_layer, time_text);
}

void remove_sensitivity_text(void *data) {
  text_layer_set_text_color(sensitivity_layer, PBL_IF_BW_ELSE(GColorBlack, GColorDukeBlue));
  sensitivity_text_timer = NULL;
}

// ----------------------------------------------------------------------- //
//  Button Functions
// ------------------------------------------------------------------------ //
void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  sensitivity_index = (sensitivity_index + 1) % sensitivity_count;
  sensitivity = sensitivity_lookup[sensitivity_index];
  text_layer_set_text(sensitivity_layer, sensitivity_text[sensitivity_index]);  // Text won't be updated on screen until next refresh, which happens 4 times per second
  text_layer_set_text_color(sensitivity_layer, GColorWhite);
  
  // Clear the sample buffer to reset BPM to 0
  offset = 1000;
  
  // Remove text after a while
  if(sensitivity_text_timer) app_timer_cancel(sensitivity_text_timer);
  sensitivity_text_timer = app_timer_register(sensitivity_text_removal_delay, remove_sensitivity_text, NULL);
}

void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// ----------------------------------------------------------------------- //
//  Main Functions
// ------------------------------------------------------------------------ //
void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);
  
  // setup BPM text layer
  frame.origin.y = (frame.size.h - 40) / 2;
  frame.size.h = 40;
  bpm_layer = text_layer_create(frame);
  text_layer_set_background_color(bpm_layer, GColorClear);
  text_layer_set_text_color(bpm_layer, GColorWhite);
  text_layer_set_text_alignment(bpm_layer, GTextAlignmentCenter);
  text_layer_set_font(bpm_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(bpm_layer, bpm_text);
  layer_add_child(window_layer, text_layer_get_layer(bpm_layer));
  
  // setup sensitivity text layer
  frame.origin.y += 40;
  frame.size.h = 80;
  sensitivity_layer = text_layer_create(frame);
  text_layer_set_background_color(sensitivity_layer, GColorClear);
  text_layer_set_text_color(sensitivity_layer, GColorWhite);
  text_layer_set_text_alignment(sensitivity_layer, GTextAlignmentCenter);
  text_layer_set_font(sensitivity_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text(sensitivity_layer, sensitivity_text[sensitivity_index]);
  layer_add_child(window_layer, text_layer_get_layer(sensitivity_layer));

  // setup time text layer
  frame.origin.y = 0;
  frame.size.h = 30;
  time_layer = text_layer_create(frame);
  text_layer_set_background_color(time_layer, GColorWhite);
  text_layer_set_text_color(time_layer, GColorBlack);
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(time_layer, time_text);
  layer_add_child(window_layer, text_layer_get_layer(time_layer));
  

  // configure buttons
  window_set_click_config_provider(window, click_config_provider);
}

void main_window_unload(Window *window) {
  text_layer_destroy(bpm_layer);
  text_layer_destroy(sensitivity_layer);
  text_layer_destroy(time_layer);
}

static void init(void) {
  // read persistent storage
  int version = persist_exists(PERSIST_VERSION_KEY) ? persist_read_int(PERSIST_VERSION_KEY) : CURRENT_VERSION;
  if(version==2)
    sensitivity_index = persist_exists(PERSIST_SENSITIVITY_KEY) ? persist_read_int(PERSIST_SENSITIVITY_KEY) : PBL_IF_RECT_ELSE(2, 1);
  
  // setup and push main window
  main_window = window_create();
  window_set_window_handlers(main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_set_background_color(main_window, PBL_IF_BW_ELSE(GColorBlack, GColorDukeBlue));
  window_stack_push(main_window, true /* Animated */);   // Display window

  // set sensitivity
  sensitivity = sensitivity_lookup[sensitivity_index];
  
  // set text disappearing timer
  if(sensitivity_text_timer) app_timer_cancel(sensitivity_text_timer);
  sensitivity_text_timer = app_timer_register(sensitivity_text_removal_delay, remove_sensitivity_text, NULL);

  // setup acceleromter (100 samples per second, update bpm text 4 times per second)
  accel_data_service_subscribe(25, accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}

static void deinit(void) {
  if(sensitivity_text_timer) app_timer_cancel(sensitivity_text_timer);
  accel_data_service_unsubscribe();
  
  // save persistent storage
  persist_write_int(PERSIST_VERSION_KEY, CURRENT_VERSION);
  persist_write_int(PERSIST_SENSITIVITY_KEY, sensitivity_index);
  
  window_destroy(main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
