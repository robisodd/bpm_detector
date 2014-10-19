#include "pebble.h"
// how long to discard old taps
//   when accelerometer is at 100Hz, this is in 10s of ms.  (e.g. 300 = 3 seconds)
//   if this is smaller, it'll react quicker but you won't be able to measure slow bpm (like < 60bpm)
#define samples_to_collect 300

static Window *main_window;
    TextLayer *bpm_layer;
   static char bpm_text[] = " 0 bpm ";
       int16_t offset = 1000;
       int16_t buffer[samples_to_collect];

int16_t abs16(int16_t x) {return (x ^ (x >> 15)) - (x >> 15);}  // quick 16-bit absolue value function

void accel_handler(AccelData *data, uint32_t num_samples) {
  uint32_t lastpoint = 1000, count = 0, bpm = 0;
  
  // if this is the first time this function is called, fill buffer with current data (this is to stop detecting 0-to-data as a tap)
  if(offset == 1000) {
    offset = 0;
    for(uint32_t i=0; i<samples_to_collect; i++)
      buffer[i] = data[0].z >> 3;
  }
  
  // read accelerometer data and add samples to the rotating buffer
  for(uint32_t i=0; i<num_samples; i++) {
    buffer[offset] = data[i].z >> 3;             // replace oldest sample with newest (it seems accelerometer data is always evenly divisible by 8, hence the ">> 3")
    offset = (offset + 1) % samples_to_collect;  // set offset to point to, what is now, the oldest sample
  }
  
  // average the time between all taps in the sample buffer
  for(int16_t i=0; i<samples_to_collect; i++) {
    if(abs16(buffer[(i+offset)%samples_to_collect] - buffer[(i+offset+1)%samples_to_collect]) > 60) {  // if Tap Detected (magnitude between two accelerometer measurements is more than 60 (60 derived empirically))
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
  //   technically: average_samples_between_taps (samples/tap) = total_samples_between_taps / number_of_taps
  //          then: average_milliseconds_between_taps (ms/tap) = (1000 ms/sec) / (100 Hz (aka samples/sec) * average_samples_between_taps (samples/tap)
  //       finally: bpm (aka taps/min) = (60 sec/min * 1000 ms/sec) / (average_milliseconds_between_taps (samples/tap))
  
  if(bpm > 999) bpm = 999;     // just in case (shouldn't be needed).  bpm is unsigned so it'll never go < 0, so no need to check for that
  
  // display the text
  snprintf(bpm_text, sizeof(bpm_text), "%ld bpm", bpm);
  text_layer_set_text(bpm_layer, bpm_text);
}
  
// ----------------------------------------------------------------------- //
//  Main Functions
// ------------------------------------------------------------------------ //
static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  // setup BPM text layer
  bpm_layer = text_layer_create(GRect(0, 60, 143, 40));
  text_layer_set_background_color(bpm_layer, GColorClear);
  text_layer_set_text_color(bpm_layer, GColorWhite);
  text_layer_set_text_alignment(bpm_layer, GTextAlignmentCenter);
  text_layer_set_font(bpm_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text(bpm_layer, bpm_text);
  layer_add_child(window_layer, text_layer_get_layer(bpm_layer));
}

static void main_window_unload(Window *window) {
  text_layer_destroy(bpm_layer);
}

static void init(void) {
  // setup and push main window
  main_window = window_create();
  window_set_window_handlers(main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_set_background_color(main_window, GColorBlack);
  window_stack_push(main_window, true /* Animated */);   // Display window

  // setup acceleromter (100 samples per second, update bpm text 4 times per second)
  accel_data_service_subscribe(25, accel_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_100HZ);
}
  
static void deinit(void) {
  accel_data_service_unsubscribe();
  window_destroy(main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
