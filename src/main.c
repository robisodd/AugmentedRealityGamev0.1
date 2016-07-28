#include <pebble.h>

// ------------------------------------------------------------------------ //
//  Structs, Macros and Globals
// ------------------------------------------------------------------------ //
#define UPDATE_MS 50 // Refresh rate in milliseconds
#define MAXDOTS 5000

#define abs(x) ((x)<0 ? -(x) : (x))

typedef struct GPoint3D {
  int16_t x;
  int16_t y;
  int16_t z;
} GPoint3D;

//Ecliptic coordinate system - point
typedef struct point_struct {
  int16_t dec;   // declination
  int16_t ra;    // right ascension
} point_struct;
static point_struct dots[MAXDOTS];

typedef struct cam_struct {
  int16_t pitch;   // [-16384, 16384] -16384 = bottom up (face down) / +16384 = bottom down (face up) / 0 = bottom & face facing horizon
  int16_t roll;    // [-32768, 32767] tilt (aileron roll) +16384=ccw (left side down), -16384 = cw (right side down)
  int16_t yaw;     // [-32768, 32767] compass direction facing
} cam_struct;
static cam_struct cam;

static Window *main_window;
static Layer  *sky_layer;

int8_t zoom=8;
uint16_t total_dots=0;




// ------------------------------------------------------------------------ //
//  Dot Functions
// ------------------------------------------------------------------------ //
void add_dot(int16_t ra, int16_t dec) {
  dots[total_dots].dec = dec;
  dots[total_dots].ra  = ra;
  total_dots++;
  if(total_dots==MAXDOTS) total_dots--;
}


// Convert Ecliptic Coordinates (RA & DEC) to 3D Rectangular Coordinates (x, y, z) to 2D Screen Coordinates (x, y)
bool get_point(int16_t ra, int16_t dec, GPoint *point) {
  //if(dots[dot].ra==0 && dots[dot].dec==0) return false;
  int32_t x, y, z, z1;
  //int16_t ra;
  int32_t cos1, cos2, sin2;

  ra   = ra + cam.yaw;
  y    = sin_lookup(dec + (TRIG_MAX_ANGLE/2)) >> 8;
  cos1 = cos_lookup(dec) >> 8;
  z1   = (cos1 * (cos_lookup(ra) >> 8)) >> 8;

  cos2 = cos_lookup(cam.pitch)>>8;
  sin2 = sin_lookup(cam.pitch)>>8;

  z = y*sin2 + z1*cos2;

  if(z <= 0) return false;    // If behind camera

  y = (y*cos2 - z1*sin2)>>8;
  x = (cos1 * (sin_lookup(ra) >> 8)) >> 8;

  cos2 = cos_lookup(cam.roll)>>8;
  sin2 = sin_lookup(cam.roll)>>8;

  point->x = (( (x*cos2 - y*sin2) << zoom) / z) + (144/2);
  point->y = (( (x*sin2 + y*cos2) << zoom) / z) + (168/2);
  
  if(point->x < 0 || point->x > 143 || point->y < 0 || point->y > 167)
    return false;
  
  return true;
}


void init_dots() {
  add_dot(   0, 0 + 2048);
  add_dot(   0, 0 + 2048 + 2048);
  add_dot(2048, 0 + 2048);
//   add_dot(   0, -16384 + 2048);
//   add_dot(   0, -16384 + 2048 + 2048);
//   add_dot(2048, -16384 + 2048);
  
  for(int16_t declination=-16384; declination<=16384; declination+=2048) // All Lattitudes
    for(int32_t right_ascension=0; right_ascension<TRIG_MAX_ANGLE; right_ascension+=2048)
      add_dot(right_ascension, declination);
    
  APP_LOG(APP_LOG_LEVEL_INFO, "total_dots = %d", total_dots);
}

// ------------------------------------------------------------------------ //
//  Triangle Functions
// ------------------------------------------------------------------------ //
static GPath *triangle_gpath = NULL;
static const GPathInfo GPATH_INFO = {3, (GPoint []){{0,0},{0,0},{0,0}}};

void init_triangle() {
  triangle_gpath = gpath_create(&GPATH_INFO);
}


void draw_triangle(GContext* ctx) {
  if(triangle_gpath) {
    // Fill the path:
    graphics_context_set_fill_color(ctx, GColorGreen);
    gpath_draw_filled(ctx, triangle_gpath);
    // Stroke the path:
    graphics_context_set_stroke_color(ctx, GColorRed);
    gpath_draw_outline(ctx, triangle_gpath);
  }
}



bool create_triangle(int16_t pt1, int16_t pt2, int16_t pt3) {
  // using single bar | instead of || to disable short-circuiting
  if(get_point(dots[pt1].ra, dots[pt1].dec, &(triangle_gpath->points[0])) |
     get_point(dots[pt2].ra, dots[pt2].dec, &(triangle_gpath->points[1])) |
     get_point(dots[pt3].ra, dots[pt3].dec, &(triangle_gpath->points[2]))) {
    return true;
  }
  return false;
}




// ------------------------------------------------------------------------ //
//  Update Functions
// ------------------------------------------------------------------------ //

void update_camera() {
  static AccelData accel; accel_service_peek(&accel);                          // Read accelerometer
  if(accel.z > 1024) accel.z = 1024; if(accel.z <-1024) accel.z =-1024; // -1024 and 1024 are top and bottom
  
  int16_t angle;
  //angle = TRIG_MAX_ANGLE - atan2_lookup(accel.z, -1 * accel.y);        // real-time pitch (jittery)
  angle  = TRIG_MAX_ANGLE - (accel.z*16);                              // real-time pitch (jittery)   (just using z works better)
  cam.pitch += ((angle - cam.pitch) / 8) + ((angle - cam.pitch) % 8);  // low-pass filter to reduce jitters

  angle  = TRIG_MAX_ANGLE - atan2_lookup(accel.x, -1 * accel.y);       // Real-time roll (jittery)
  cam.roll  += ((angle - cam.roll) / 8) + ((angle - cam.roll) % 8);    // low-pass filter to reduce jitters

  cam.yaw += angle>>4;  // Move Yaw depending on watch tilt (roll).  Adjust ">>value" to change speed of rotation
}



static void timer_callback(void *data) {
  update_camera();
  layer_mark_dirty(sky_layer);  // Schedule redraw of screen
}



// ------------------------------------------------------------------------ //
//  Button Functions
// ------------------------------------------------------------------------ //
void     up_single_click_handler(ClickRecognizerRef recognizer, void *context) {zoom++;}
void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  add_dot(TRIG_MAX_ANGLE - cam.yaw, TRIG_MAX_ANGLE - cam.pitch);

    printf("point0 = (%d, %d)", triangle_gpath->points[0].x, triangle_gpath->points[0].y);
    printf("point1 = (%d, %d)", triangle_gpath->points[1].x, triangle_gpath->points[1].y);
    printf("point2 = (%d, %d)", triangle_gpath->points[2].x, triangle_gpath->points[2].y);
}
void   down_single_click_handler(ClickRecognizerRef recognizer, void *context) {zoom--;}
  
void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,         up_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,     down_single_click_handler);
}




// ------------------------------------------------------------------------ //
//  Drawing Functions
// ------------------------------------------------------------------------ //
static void draw_textbox(GContext *ctx, char *text, GFont font, GRect rect, GTextAlignment alignment, GColor bg_color, GColor border_color, GColor text_color) {
  graphics_context_set_text_color (ctx, GColorWhite);
  graphics_context_set_fill_color (ctx, GColorDukeBlue);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
  graphics_draw_rect(ctx, rect);

  rect.origin.y -= 1;
  rect.origin.x += 1;
  rect.size.w -= 2;
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeWordWrap, alignment, NULL);
}

static void draw_text(GContext *ctx) {
  static char textbuffer[100];  //Buffer to hold text
  
  int16_t ra  = (((((cam.yaw  >>9)*-360)>>7))+360)%360;  //(TRIG_MAX_ANGLE   - cam.yaw  ) / 360;
  int16_t dec =     ((cam.pitch>>9)*-360)>>7;            //(TRIG_MAX_ANGLE/2 - cam.pitch) / 360;
  
  snprintf(textbuffer, sizeof(textbuffer), "ra: %dº\ndec: %dº\nzoom: %d", ra, dec, zoom);
  draw_textbox(ctx, textbuffer, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 0, 60, 48), GTextAlignmentLeft, GColorDukeBlue, GColorWhite, GColorWhite);
  
  snprintf(textbuffer, sizeof(textbuffer), "pitch: %dº\nroll: %dº\nyaw: %dº", cam.pitch, cam.roll, cam.yaw);
  draw_textbox(ctx, textbuffer, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(144-80, 0, 80, 48), GTextAlignmentRight, GColorDukeBlue, GColorWhite, GColorWhite);
}



static void draw_landscape(GContext *ctx) {
  int x_center, y_center;
  int color1, color2;
  #define sky 0b11000011
  #define grass 0b11000100
  uint8_t *screen8 = (uint8_t*)*(uintptr_t*)ctx;                     //  8bit Pointer to frame buffer (i.e. screen RAM), for color use only
  
  int cos_roll = cos_lookup(cam.roll);
  int sin_roll = sin_lookup(cam.roll);
  int cos_pitch = cos_lookup(TRIG_MAX_ANGLE - cam.pitch);  // z3D
  int sin_pitch = sin_lookup(TRIG_MAX_ANGLE - cam.pitch);  // y3D

  

  if(abs(cos_roll) > abs(sin_roll)) {    // Normal horizontal horizon: go from x=0 to x=144
    // Horizon: Y of Center Dot at X=0 (screen center)
    color1 = cos_roll>0 ? grass : sky; // top side
    color2 = cos_roll>0 ? sky : grass; // bottom side
    y_center = ((sin_pitch<<zoom) / cos_pitch);                // y2D = y3D / z3D
    for(int x = 0; x<144; x++) {
      int y_start = y_center + (((x-144/2)*sin_roll) / cos_roll) + 168/2;
      for (int y = 0; y<168; y++)
        screen8[(y*144) + x] = (y>y_start) ? color1 : color2;
    }
  } else {                               // Weird vertical horizon: go from y=0 to y=168
    // Horizon: X of Center Dot at Y=0 (screen center)
    color1 = sin_roll<0 ? grass : sky;  // left side
    color2 = sin_roll<0 ? sky : grass;  // right side
    x_center = ((sin_pitch<<zoom) / cos_pitch);
    for(int y = 0; y<168; y++) {
      int x_start = x_center + (((y-168/2)*cos_roll) / sin_roll) + 144/2;
      for (int x = 0; x<144; x++)
        screen8[(y*144) + x] = (x>x_start) ? color1 : color2;
    }
  }

}


/*
static void draw_horizon(GContext *ctx) {
  int cos, sin, x_center, y_center, x, y;
  graphics_context_set_fill_color (ctx, GColorYellow);
  graphics_context_set_stroke_color (ctx, GColorYellow);
  
    
    int cos_roll = cos_lookup(cam.roll);
    int sin_roll = sin_lookup(cam.roll);
    int cos_pitch = cos_lookup(TRIG_MAX_ANGLE - cam.pitch);  // z3D
    int sin_pitch = sin_lookup(TRIG_MAX_ANGLE - cam.pitch);  // y3D
    
    if(abs(cos_roll) > abs(sin_roll)) {
      // Normal horizontal horizon: go from x=0 to x=144
      
      // Horizon: Center Dot
      y_center = ((sin_pitch<<zoom) / cos_pitch);                // y2D = y3D / z3D
      x_center = 0;
      graphics_fill_circle(ctx, GPoint(x_center + 144/2, y_center + 168/2), 10);

      
      // Horizon: Left Edge of Screen
      x = x_center - (144/2);
      y = y_center + ((x*sin_roll) / cos_roll);
      graphics_fill_circle(ctx, GPoint(x + 144/2, y + 168/2), 10);
      // Horizon: Right Edge of Screen
      x = x_center + (144/2);
      y = y_center + ((x*sin_roll) / cos_roll);
      graphics_fill_circle(ctx, GPoint(x + 144/2, y + 168/2), 10);
    } else {
      // Weird vertical horizon: go from y=0 to y=168
      
      // Horizon: Center Dot
      x_center = ((sin_pitch<<zoom) / cos_pitch);
      y_center = 0;
      graphics_fill_circle(ctx, GPoint(x_center + 144/2, y_center + 168/2), 10);

      
      // Horizon: Top Edge of Screen
      y = y_center - 168/2;
      x = x_center + ((y*cos_roll) / sin_roll);
      graphics_fill_circle(ctx, GPoint(x + 144/2, y + 168/2), 10);
      
      // Horizon: Bottom Edge of Screen
      y = y_center + 168/2;
      x = x_center + ((y*cos_roll) / sin_roll);
      graphics_fill_circle(ctx, GPoint(x + 144/2, y + 168/2), 10);
    }
}
*/

static void sky_layer_update(Layer *me, GContext *ctx) {
  GPoint point=(GPoint){.x=0, .y=0};

  GRect frame = layer_get_frame(me);
  
  draw_landscape(ctx);
  
  if(create_triangle(0, 1, 2)) {
    //printf("Drawing");
    draw_triangle(ctx);
  }
    
  
  graphics_context_set_stroke_color(ctx, GColorWhite);
  int16_t size = 1<<(zoom-5);
  for(int16_t i=0; i<total_dots; i++) {
    if(get_point(dots[i].ra, dots[i].dec, &point)) {
      //point.y += frame.size.h / 2;
      //point.x += frame.size.w / 2;
      if(point.x>=0 && point.x<frame.size.w && point.y>=0 && point.y<frame.size.h) { // If within screen bounds
        graphics_draw_rect(ctx, GRect(point.x - (size / 2), point.y - (size / 2), size, size));
      }
    }
  }
  
  draw_text(ctx);
  
  app_timer_register(UPDATE_MS, timer_callback, NULL); // Schedule a callback
}

// ------------------------------------------------------------------------ //
//  Main Functions
// ------------------------------------------------------------------------ //
int main(void) {
  // Create and Push Window
  main_window = window_create();
  window_set_click_config_provider(main_window, click_config_provider);
  window_set_background_color(main_window, GColorBlack);
  window_stack_push(main_window, true); // Display window.  Callback will be called once layer is dirtied then written
  
  // Create Drawing Layer
  Layer *root_layer = window_get_root_layer(main_window);
  sky_layer = layer_create(layer_get_frame(root_layer));
  layer_set_update_proc(sky_layer, sky_layer_update);
  layer_add_child(root_layer, sky_layer);
  
  // Final Inits
  srand(time(NULL));  // Seed randomizer
  accel_data_service_subscribe(0, NULL);  // We will be using the accelerometer
  init_dots();
  init_triangle();
  
  // Init Camera  (Maybe initialize to actual accelerometer/magnetometer data)
  cam.pitch=0; cam.roll=0; cam.yaw=0;

  app_event_loop();
}