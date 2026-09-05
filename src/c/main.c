// SPDX-FileCopyrightText: 2026 MrReSc
// SPDX-License-Identifier: Apache-2.0

#include <pebble.h>

#include <locale.h>
#include <string.h>

#define SCREEN_MARGIN 4
#define STATUS_MARGIN 8
#define DATE_Y 6
#define DATE_HEIGHT 30
#define TOP_STATUS_BOTTOM 42
#define TIME_HEIGHT 72
#define BOTTOM_STATUS_TWO_ROWS_Y 148
#define BOTTOM_STATUS_ONE_ROW_Y 185
#define FIRST_INFO_Y 153
#define SECOND_INFO_Y 190
#define SINGLE_INFO_Y 190
#define INFO_HEIGHT 30
#define CELL_GAP 4
#define ICON_TEXT_GAP 4
#define HEADER_GROUP_GAP 4
#define STATUS_TEXT_OPTICAL_Y_OFFSET -3
#define BATTERY_ICON_WIDTH 14
#define BATTERY_ICON_HEIGHT 22
#define BATTERY_BODY_WIDTH 14
#define BATTERY_BODY_HEIGHT 18
#define BATTERY_TERMINAL_WIDTH 6
#define BATTERY_TERMINAL_HEIGHT 4
#define BATTERY_STROKE_WIDTH 2

#define PERSIST_KEY_SUN_DATE 1
#define PERSIST_KEY_SUNRISE_MINUTES 2
#define PERSIST_KEY_SUNSET_MINUTES 3
#define PERSIST_KEY_LAST_SUN_REQUEST 4
#define SUN_RETRY_INTERVAL_SECONDS (6 * 60 * 60)

static Window *s_main_window;
static Layer *s_canvas_layer;

static GBitmap *s_heart_bitmap;
static GBitmap *s_steps_bitmap;
static GBitmap *s_sunrise_bitmap;
static GBitmap *s_sunset_bitmap;
static GBitmap *s_calendar_bitmap;

static GFont s_time_font;
static GFont s_ampm_font;
static GFont s_info_font;

static char s_date_buffer[24];
static char s_time_buffer[6];
static char s_ampm_buffer[3];
static char s_steps_buffer[12];
static char s_heart_buffer[12];
static char s_sunrise_buffer[6];
static char s_sunset_buffer[6];
static char s_battery_buffer[5];

static int32_t s_steps;
static int32_t s_heart_rate;
static int32_t s_sun_date;
static int32_t s_sunrise_minutes = -1;
static int32_t s_sunset_minutes = -1;
static int32_t s_current_date;
static time_t s_last_sun_request_time;

static bool s_steps_available;
static bool s_heart_available;
static bool s_sun_available;
static bool s_health_subscribed;
static bool s_outbox_busy;

static void prv_update_battery_state(BatteryChargeState state) {
  snprintf(s_battery_buffer, sizeof(s_battery_buffer), "%u%%",
           (unsigned int)state.charge_percent);
}

static int32_t prv_date_code(const struct tm *local_time) {
  return (local_time->tm_year + 1900) * 10000 +
      (local_time->tm_mon + 1) * 100 + local_time->tm_mday;
}

static void prv_mark_dirty(void) {
  if (s_canvas_layer) {
    layer_mark_dirty(s_canvas_layer);
  }
}

static void prv_format_sun_time(char *buffer, size_t buffer_size, int32_t minutes) {
  if (buffer_size < 6 || minutes < 0 || minutes >= 24 * 60) {
    if (buffer_size > 0) {
      buffer[0] = '\0';
    }
    return;
  }

  uint8_t hour = minutes / 60;
  uint8_t minute = minutes % 60;

  if (clock_is_24h_style()) {
    buffer[0] = '0' + hour / 10;
    buffer[1] = '0' + hour % 10;
    buffer[2] = ':';
    buffer[3] = '0' + minute / 10;
    buffer[4] = '0' + minute % 10;
    buffer[5] = '\0';
  } else {
    uint8_t display_hour = hour % 12;
    if (display_hour == 0) {
      display_hour = 12;
    }
    int index = 0;
    if (display_hour >= 10) {
      buffer[index++] = '1';
      buffer[index++] = '0' + display_hour - 10;
    } else {
      buffer[index++] = '0' + display_hour;
    }
    buffer[index++] = ':';
    buffer[index++] = '0' + minute / 10;
    buffer[index++] = '0' + minute % 10;
    buffer[index] = '\0';
  }
}

static bool prv_has_sun_result_for_current_date(void) {
  if (s_sun_date != s_current_date) {
    return false;
  }

  const bool has_times =
      s_sunrise_minutes >= 0 && s_sunrise_minutes < 24 * 60 &&
      s_sunset_minutes >= 0 && s_sunset_minutes < 24 * 60;
  const bool has_no_event_result =
      s_sunrise_minutes == -1 && s_sunset_minutes == -1;
  return has_times || has_no_event_result;
}

static void prv_refresh_sun_visibility(void) {
  s_sun_available = s_sun_date == s_current_date &&
      s_sunrise_minutes >= 0 && s_sunrise_minutes < 24 * 60 &&
      s_sunset_minutes >= 0 && s_sunset_minutes < 24 * 60;

  if (s_sun_available) {
    prv_format_sun_time(s_sunrise_buffer, sizeof(s_sunrise_buffer), s_sunrise_minutes);
    prv_format_sun_time(s_sunset_buffer, sizeof(s_sunset_buffer), s_sunset_minutes);
  } else {
    s_sunrise_buffer[0] = '\0';
    s_sunset_buffer[0] = '\0';
  }
}

static void prv_update_clock_state(const struct tm *tick_time) {
  if (strftime(s_date_buffer, sizeof(s_date_buffer), "%x", tick_time) == 0) {
    strftime(s_date_buffer, sizeof(s_date_buffer), "%Y-%m-%d", tick_time);
  }

  if (clock_is_24h_style()) {
    strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M", tick_time);
    s_ampm_buffer[0] = '\0';
  } else {
    strftime(s_time_buffer, sizeof(s_time_buffer), "%I:%M", tick_time);
    if (s_time_buffer[0] == '0') {
      memmove(s_time_buffer, s_time_buffer + 1, strlen(s_time_buffer));
    }
    strftime(s_ampm_buffer, sizeof(s_ampm_buffer), "%p", tick_time);
  }

  s_current_date = prv_date_code(tick_time);
  prv_refresh_sun_visibility();
}

static void prv_update_health_state(void) {
#ifdef PBL_HEALTH
  const time_t now = time(NULL);
  const time_t today = time_start_of_today();

  HealthServiceAccessibilityMask steps_access =
      health_service_metric_accessible(HealthMetricStepCount, today, now);
  s_steps_available = (steps_access & HealthServiceAccessibilityMaskAvailable) != 0;
  if (s_steps_available) {
    s_steps = health_service_sum_today(HealthMetricStepCount);
    snprintf(s_steps_buffer, sizeof(s_steps_buffer), "%ld", (long)s_steps);
  }

  HealthServiceAccessibilityMask heart_access =
      health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  s_heart_rate = health_service_peek_current_value(HealthMetricHeartRateBPM);
  s_heart_available =
      (heart_access & HealthServiceAccessibilityMaskAvailable) != 0 && s_heart_rate > 0;
  if (s_heart_available) {
    snprintf(s_heart_buffer, sizeof(s_heart_buffer), "%ld", (long)s_heart_rate);
  }
#else
  s_steps_available = false;
  s_heart_available = false;
#endif
}

static bool prv_has_health_row(void) {
  return s_steps_available || s_heart_available;
}

static int prv_centered_status_text_y(GRect cell, int text_height) {
  return cell.origin.y + (cell.size.h - text_height) / 2 +
      STATUS_TEXT_OPTICAL_Y_OFFSET;
}

static void prv_draw_icon_value(GContext *ctx, GBitmap *bitmap, const char *value,
                                GRect cell, GTextAlignment group_alignment) {
  if (!bitmap || !value || value[0] == '\0') {
    return;
  }

  GRect icon_bounds = gbitmap_get_bounds(bitmap);
  GSize text_size = graphics_text_layout_get_content_size(
      value, s_info_font, GRect(0, 0, cell.size.w, cell.size.h),
      GTextOverflowModeFill, GTextAlignmentLeft);
  int group_width = icon_bounds.size.w + ICON_TEXT_GAP + text_size.w;
  int group_x = cell.origin.x;
  if (group_alignment == GTextAlignmentRight) {
    group_x += cell.size.w - group_width;
  }

  icon_bounds.origin.x = group_x;
  icon_bounds.origin.y = cell.origin.y + (cell.size.h - icon_bounds.size.h) / 2;
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bitmap, icon_bounds);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, value, s_info_font,
                     GRect(group_x + icon_bounds.size.w + ICON_TEXT_GAP,
                           prv_centered_status_text_y(cell, text_size.h),
                           text_size.w + 2, text_size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
}

static void prv_draw_health_row(GContext *ctx, int y, int width) {
  const int content_width = width - 2 * STATUS_MARGIN;
  if (s_steps_available && s_heart_available) {
    const int cell_width = (content_width - CELL_GAP) / 2;
    prv_draw_icon_value(ctx, s_steps_bitmap, s_steps_buffer,
                        GRect(STATUS_MARGIN, y, cell_width, INFO_HEIGHT),
                        GTextAlignmentLeft);
    prv_draw_icon_value(ctx, s_heart_bitmap, s_heart_buffer,
                        GRect(STATUS_MARGIN + cell_width + CELL_GAP, y,
                              cell_width, INFO_HEIGHT),
                        GTextAlignmentRight);
  } else if (s_steps_available) {
    prv_draw_icon_value(ctx, s_steps_bitmap, s_steps_buffer,
                        GRect(STATUS_MARGIN, y, content_width, INFO_HEIGHT),
                        GTextAlignmentLeft);
  } else if (s_heart_available) {
    prv_draw_icon_value(ctx, s_heart_bitmap, s_heart_buffer,
                        GRect(STATUS_MARGIN, y, content_width, INFO_HEIGHT),
                        GTextAlignmentRight);
  }
}

static void prv_draw_sun_row(GContext *ctx, int y, int width) {
  const int content_width = width - 2 * STATUS_MARGIN;
  const int cell_width = (content_width - CELL_GAP) / 2;
  prv_draw_icon_value(ctx, s_sunrise_bitmap, s_sunrise_buffer,
                      GRect(STATUS_MARGIN, y, cell_width, INFO_HEIGHT),
                      GTextAlignmentLeft);
  prv_draw_icon_value(ctx, s_sunset_bitmap, s_sunset_buffer,
                      GRect(STATUS_MARGIN + cell_width + CELL_GAP, y,
                            cell_width, INFO_HEIGHT),
                      GTextAlignmentRight);
}

static void prv_draw_battery_icon(GContext *ctx, GRect bounds) {
  const GRect body = GRect(bounds.origin.x,
                           bounds.origin.y + BATTERY_TERMINAL_HEIGHT,
                           BATTERY_BODY_WIDTH, BATTERY_BODY_HEIGHT);
  const GRect terminal = GRect(
      bounds.origin.x + (BATTERY_BODY_WIDTH - BATTERY_TERMINAL_WIDTH) / 2,
      bounds.origin.y, BATTERY_TERMINAL_WIDTH, BATTERY_TERMINAL_HEIGHT);
  const GRect interior = GRect(
      body.origin.x + BATTERY_STROKE_WIDTH,
      body.origin.y + BATTERY_STROKE_WIDTH,
      body.size.w - 2 * BATTERY_STROKE_WIDTH,
      body.size.h - 2 * BATTERY_STROKE_WIDTH);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, body, 0, GCornerNone);
  graphics_fill_rect(ctx, terminal, 0, GCornerNone);

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, interior, 0, GCornerNone);
}

static void prv_draw_header(GContext *ctx, int width) {
  const int left = STATUS_MARGIN;
  const int right = width - STATUS_MARGIN;
  const GRect header_cell = GRect(0, DATE_Y, width, DATE_HEIGHT);
  const GRect battery_bounds = GRect(
      left, DATE_Y + (DATE_HEIGHT - BATTERY_ICON_HEIGHT) / 2,
      BATTERY_ICON_WIDTH, BATTERY_ICON_HEIGHT);
  prv_draw_battery_icon(ctx, battery_bounds);

  GSize battery_text_size = graphics_text_layout_get_content_size(
      s_battery_buffer, s_info_font, GRect(0, 0, 48, DATE_HEIGHT),
      GTextOverflowModeFill, GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_battery_buffer, s_info_font,
                     GRect(left + BATTERY_ICON_WIDTH + ICON_TEXT_GAP,
                           prv_centered_status_text_y(header_cell,
                                                      battery_text_size.h),
                           battery_text_size.w + 2, battery_text_size.h),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);

  GSize date_size = graphics_text_layout_get_content_size(
      s_date_buffer, s_info_font, GRect(0, 0, right - left, DATE_HEIGHT),
      GTextOverflowModeFill, GTextAlignmentLeft);
  GRect calendar_bounds = GRect(right, DATE_Y, 0, 0);
  if (s_calendar_bitmap) {
    calendar_bounds = gbitmap_get_bounds(s_calendar_bitmap);
  }
  const int date_group_width = calendar_bounds.size.w + ICON_TEXT_GAP + date_size.w;
  int date_group_x = right - date_group_width;
  const int battery_group_right = left + BATTERY_ICON_WIDTH + ICON_TEXT_GAP +
      battery_text_size.w;
  if (date_group_x < battery_group_right + HEADER_GROUP_GAP) {
    date_group_x = battery_group_right + HEADER_GROUP_GAP;
  }

  if (s_calendar_bitmap) {
    calendar_bounds.origin.x = date_group_x;
    calendar_bounds.origin.y = DATE_Y + (DATE_HEIGHT - calendar_bounds.size.h) / 2;
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_calendar_bitmap, calendar_bounds);
  }

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, s_date_buffer, s_info_font,
                     GRect(date_group_x + calendar_bounds.size.w + ICON_TEXT_GAP,
                           prv_centered_status_text_y(header_cell, date_size.h),
                           right - date_group_x - calendar_bounds.size.w -
                               ICON_TEXT_GAP,
                           date_size.h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void prv_draw_horizontal_dotted_line(GContext *ctx, int y, int width) {
  const int first_x = y % 2;
  for (int x = first_x; x < width; x += 2) {
    graphics_draw_pixel(ctx, GPoint(x, y));
  }
}

static void prv_draw_status_background(GContext *ctx, GRect bounds) {
  // Emery has no solid neutral gray between #AAAAAA and white. One gray
  // pixel per 2x2 white pixels gives a very light, neutral appearance.
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);
  for (int y = bounds.origin.y; y < bounds.origin.y + bounds.size.h; ++y) {
    if (y % 2 != 0) {
      continue;
    }
    for (int x = bounds.origin.x; x < bounds.origin.x + bounds.size.w; x += 2) {
      graphics_draw_pixel(ctx, GPoint(x, y));
    }
  }
}

static void prv_draw_time(GContext *ctx, int width, int time_y) {
  graphics_context_set_text_color(ctx, GColorBlack);

  const int glyph_count = strlen(s_time_buffer);
  const int target_width = glyph_count == 5 ? width - 16 : width - 36;
  int glyph_widths[5] = {0};
  int glyphs_width = 0;
  char glyph[2] = {'\0', '\0'};

  for (int i = 0; i < glyph_count; ++i) {
    glyph[0] = s_time_buffer[i];
    glyph_widths[i] = graphics_text_layout_get_content_size(
        glyph, s_time_font, GRect(0, 0, 54, TIME_HEIGHT),
        GTextOverflowModeFill, GTextAlignmentLeft).w;
    glyphs_width += glyph_widths[i];
  }

  const int gap_count = glyph_count - 1;
  const int gap_space = target_width - glyphs_width;
  const int base_gap = gap_count > 0 ? gap_space / gap_count : 0;
  const int extra_gap_pixels = gap_count > 0 ? gap_space % gap_count : 0;
  int x = (width - target_width) / 2;

  for (int i = 0; i < glyph_count; ++i) {
    glyph[0] = s_time_buffer[i];
    graphics_draw_text(ctx, glyph, s_time_font,
                       GRect(x, time_y, glyph_widths[i] + 2, TIME_HEIGHT),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    x += glyph_widths[i];
    if (i < gap_count) {
      x += base_gap + (i < extra_gap_pixels ? 1 : 0);
    }
  }

  if (!clock_is_24h_style() && s_ampm_buffer[0] != '\0') {
    GSize ampm_size = graphics_text_layout_get_content_size(
        s_ampm_buffer, s_ampm_font, GRect(0, 0, 30, 20),
        GTextOverflowModeFill, GTextAlignmentLeft);
    const int ampm_x = width - SCREEN_MARGIN - ampm_size.w;

    graphics_draw_text(ctx, s_ampm_buffer, s_ampm_font,
                       GRect(ampm_x, time_y - 15, ampm_size.w + 1, 20),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);
  const GRect unobstructed = layer_get_unobstructed_bounds(layer);
  const int width = bounds.size.w;
  const bool health_row = prv_has_health_row();
  const bool sun_row = s_sun_available;
  const bool has_status_row = health_row || sun_row;
  const bool has_two_status_rows = health_row && sun_row;
  const int bottom_status_y = has_two_status_rows ?
      BOTTOM_STATUS_TWO_ROWS_Y : BOTTOM_STATUS_ONE_ROW_Y;
  const int time_area_top = TOP_STATUS_BOTTOM + 1;
  const int time_area_bottom = has_status_row ? bottom_status_y : bounds.size.h;
  const int time_y = time_area_top +
      (time_area_bottom - time_area_top - TIME_HEIGHT) / 2;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  prv_draw_status_background(ctx, GRect(0, 0, width, TOP_STATUS_BOTTOM + 1));
  if (has_status_row) {
    prv_draw_status_background(ctx, GRect(0, bottom_status_y, width,
                                         bounds.size.h - bottom_status_y));
  }

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  prv_draw_horizontal_dotted_line(ctx, TOP_STATUS_BOTTOM, width);
  if (has_status_row) {
    prv_draw_horizontal_dotted_line(ctx, bottom_status_y, width);
  }

  prv_draw_header(ctx, width);

  prv_draw_time(ctx, width, time_y);

  if (has_two_status_rows) {
    if (FIRST_INFO_Y + INFO_HEIGHT <= unobstructed.size.h) {
      prv_draw_health_row(ctx, FIRST_INFO_Y, width);
    }
    if (SECOND_INFO_Y + INFO_HEIGHT <= unobstructed.size.h) {
      prv_draw_sun_row(ctx, SECOND_INFO_Y, width);
    }
  } else if (health_row) {
    if (SINGLE_INFO_Y + INFO_HEIGHT <= unobstructed.size.h) {
      prv_draw_health_row(ctx, SINGLE_INFO_Y, width);
    }
  } else if (sun_row && SINGLE_INFO_Y + INFO_HEIGHT <= unobstructed.size.h) {
    prv_draw_sun_row(ctx, SINGLE_INFO_Y, width);
  }
}

static void prv_request_sun_times(void) {
  if (s_outbox_busy) {
    return;
  }

  // Throttle failed attempts as well, including an unavailable outbox.
  s_last_sun_request_time = time(NULL);
  persist_write_int(PERSIST_KEY_LAST_SUN_REQUEST,
                    (int32_t)s_last_sun_request_time);

  DictionaryIterator *iterator;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Sun request begin failed: %d", (int)result);
    return;
  }

  dict_write_uint8(iterator, MESSAGE_KEY_REQUEST_SUN, 1);
  result = app_message_outbox_send();
  if (result == APP_MSG_OK) {
    s_outbox_busy = true;
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Sun request send failed: %d", (int)result);
  }
}

static bool prv_sun_retry_is_due(void) {
  if (prv_has_sun_result_for_current_date()) {
    return false;
  }

  const time_t now = time(NULL);
  return s_last_sun_request_time <= 0 || now < s_last_sun_request_time ||
      now - s_last_sun_request_time >= SUN_RETRY_INTERVAL_SECONDS;
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  const int32_t previous_date = s_current_date;
  prv_update_clock_state(tick_time);
  prv_update_health_state();
  prv_mark_dirty();

  const bool day_changed = previous_date != s_current_date;
  const bool retry_boundary =
      tick_time->tm_min == 0 && tick_time->tm_hour % 6 == 0;
  if ((day_changed && !prv_has_sun_result_for_current_date()) ||
      (retry_boundary && prv_sun_retry_is_due())) {
    prv_request_sun_times();
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSignificantUpdate ||
      event == HealthEventMovementUpdate ||
      event == HealthEventHeartRateUpdate) {
    prv_update_health_state();
    prv_mark_dirty();
  }
}

static void prv_battery_handler(BatteryChargeState state) {
  prv_update_battery_state(state);
  prv_mark_dirty();
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  Tuple *ready_tuple = dict_find(iterator, MESSAGE_KEY_JS_READY);
  if (ready_tuple && prv_sun_retry_is_due()) {
    prv_request_sun_times();
  }

  Tuple *sunrise_tuple = dict_find(iterator, MESSAGE_KEY_SUNRISE_MINUTES);
  Tuple *sunset_tuple = dict_find(iterator, MESSAGE_KEY_SUNSET_MINUTES);
  Tuple *date_tuple = dict_find(iterator, MESSAGE_KEY_SUN_DATE);
  if (!sunrise_tuple || !sunset_tuple || !date_tuple) {
    return;
  }

  const int32_t sunrise = sunrise_tuple->value->int32;
  const int32_t sunset = sunset_tuple->value->int32;
  const int32_t date = date_tuple->value->int32;
  const bool has_times =
      sunrise >= 0 && sunrise < 24 * 60 &&
      sunset >= 0 && sunset < 24 * 60;
  const bool has_no_events = sunrise == -1 && sunset == -1;
  if ((!has_times && !has_no_events) || date <= 0) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Invalid sun data");
    return;
  }

  s_sunrise_minutes = sunrise;
  s_sunset_minutes = sunset;
  s_sun_date = date;
  persist_write_int(PERSIST_KEY_SUNRISE_MINUTES, sunrise);
  persist_write_int(PERSIST_KEY_SUNSET_MINUTES, sunset);
  persist_write_int(PERSIST_KEY_SUN_DATE, date);
  prv_refresh_sun_visibility();
  prv_mark_dirty();
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Inbox dropped: %d", (int)reason);
}

static void prv_outbox_sent(DictionaryIterator *iterator, void *context) {
  s_outbox_busy = false;
}

static void prv_outbox_failed(DictionaryIterator *iterator, AppMessageResult reason,
                              void *context) {
  s_outbox_busy = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "Outbox failed: %d", (int)reason);
}

static void prv_load_persisted_sun(void) {
  if (persist_exists(PERSIST_KEY_SUN_DATE) &&
      persist_exists(PERSIST_KEY_SUNRISE_MINUTES) &&
      persist_exists(PERSIST_KEY_SUNSET_MINUTES)) {
    s_sun_date = persist_read_int(PERSIST_KEY_SUN_DATE);
    s_sunrise_minutes = persist_read_int(PERSIST_KEY_SUNRISE_MINUTES);
    s_sunset_minutes = persist_read_int(PERSIST_KEY_SUNSET_MINUTES);
  }
  if (persist_exists(PERSIST_KEY_LAST_SUN_REQUEST)) {
    s_last_sun_request_time =
        (time_t)persist_read_int(PERSIST_KEY_LAST_SUN_REQUEST);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);

  s_time_font = fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);
  s_ampm_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_info_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

  s_heart_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HEART);
  s_steps_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_STEPS);
  s_sunrise_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SUNRISE);
  s_sunset_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SUNSET);
  s_calendar_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CALENDAR);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
  s_canvas_layer = NULL;

  gbitmap_destroy(s_heart_bitmap);
  gbitmap_destroy(s_steps_bitmap);
  gbitmap_destroy(s_sunrise_bitmap);
  gbitmap_destroy(s_sunset_bitmap);
  gbitmap_destroy(s_calendar_bitmap);
  s_heart_bitmap = NULL;
  s_steps_bitmap = NULL;
  s_sunrise_bitmap = NULL;
  s_sunset_bitmap = NULL;
  s_calendar_bitmap = NULL;
}

static void prv_init(void) {
  setlocale(LC_ALL, "");
  prv_load_persisted_sun();

  time_t now = time(NULL);
  struct tm *local_time = localtime(&now);
  if (local_time) {
    prv_update_clock_state(local_time);
  }
  prv_update_health_state();
  prv_update_battery_state(battery_state_service_peek());

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorWhite);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload
  });
  window_stack_push(s_main_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  battery_state_service_subscribe(prv_battery_handler);
#ifdef PBL_HEALTH
  s_health_subscribed = health_service_events_subscribe(prv_health_handler, NULL);
#endif

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  AppMessageResult app_message_result = app_message_open(128, 64);
  if (app_message_result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage open failed: %d", (int)app_message_result);
  }
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#ifdef PBL_HEALTH
  if (s_health_subscribed) {
    health_service_events_unsubscribe();
  }
#endif
  app_message_deregister_callbacks();
  window_destroy(s_main_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
