/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2018 - Michael Lelli
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_assert.h>
#include <retro_miscellaneous.h>
#include <encodings/crc32.h>
#include <encodings/utf.h>

#ifdef WEB_SCALING
#include <emscripten/emscripten.h>
#endif

#include <emscripten/html5.h>

#include "../input_keymaps.h"

#include "../../tasks/tasks_internal.h"
#include "../../configuration.h"
#include "../../retroarch.h"
#include "../../verbosity.h"

/* https://developer.mozilla.org/en-US/docs/Web/API/MouseEvent/button */
#define RWEBINPUT_MOUSE_BTNL 0
#define RWEBINPUT_MOUSE_BTNM 1
#define RWEBINPUT_MOUSE_BTNR 2
#define RWEBINPUT_MOUSE_BTN4 3
#define RWEBINPUT_MOUSE_BTN5 4

typedef struct rwebinput_key_to_code_map_entry
{
   const char *key;
   enum retro_key rk;
} rwebinput_key_to_code_map_entry_t;

typedef struct rwebinput_keyboard_event
{
   int type;
   EmscriptenKeyboardEvent event;
} rwebinput_keyboard_event_t;

typedef struct rwebinput_keyboard_event_queue
{
   rwebinput_keyboard_event_t *events;
   size_t count;
   size_t max_size;
} rwebinput_keyboard_event_queue_t;

typedef struct rwebinput_touch
{
   long touch_id;
   long last_canvasX;
   long last_canvasY;
   bool down;
   long last_touchdown_id;
   long last_touchdown_location;
   bool clicked_yet;
} rwebinput_touch_t;

typedef struct rwebinput_mouse_states
{
   double pending_scroll_x;
   double pending_scroll_y;
   double scroll_x;
   double scroll_y;
   signed x;
   signed y;
   signed pending_delta_x;
   signed pending_delta_y;
   signed delta_x;
   signed delta_y;
   uint8_t buttons;
} rwebinput_mouse_state_t;

typedef struct rwebinput_input
{
   rwebinput_touch_t touch;
   rwebinput_mouse_state_t mouse;             /* double alignment */
   rwebinput_keyboard_event_queue_t keyboard; /* ptr alignment */
   bool keys[RETROK_LAST];
} rwebinput_input_t;

/* KeyboardEvent.keyCode has been deprecated for a while and doesn't have
 * separate left/right modifer codes, so we have to map string labels from
 * KeyboardEvent.code to retro keys */
static const rwebinput_key_to_code_map_entry_t rwebinput_key_to_code_map[] =
{
   { "KeyA", RETROK_a },
   { "KeyB", RETROK_b },
   { "KeyC", RETROK_c },
   { "KeyD", RETROK_d },
   { "KeyE", RETROK_e },
   { "KeyF", RETROK_f },
   { "KeyG", RETROK_g },
   { "KeyH", RETROK_h },
   { "KeyI", RETROK_i },
   { "KeyJ", RETROK_j },
   { "KeyK", RETROK_k },
   { "KeyL", RETROK_l },
   { "KeyM", RETROK_m },
   { "KeyN", RETROK_n },
   { "KeyO", RETROK_o },
   { "KeyP", RETROK_p },
   { "KeyQ", RETROK_q },
   { "KeyR", RETROK_r },
   { "KeyS", RETROK_s },
   { "KeyT", RETROK_t },
   { "KeyU", RETROK_u },
   { "KeyV", RETROK_v },
   { "KeyW", RETROK_w },
   { "KeyX", RETROK_x },
   { "KeyY", RETROK_y },
   { "KeyZ", RETROK_z },
   { "ArrowLeft", RETROK_LEFT },
   { "ArrowRight", RETROK_RIGHT },
   { "ArrowUp", RETROK_UP },
   { "ArrowDown", RETROK_DOWN },
   { "Enter", RETROK_RETURN },
   { "NumpadEnter", RETROK_KP_ENTER },
   { "Tab", RETROK_TAB },
   { "Insert", RETROK_INSERT },
   { "Delete", RETROK_DELETE },
   { "End", RETROK_END },
   { "Home", RETROK_HOME },
   { "ShiftRight", RETROK_RSHIFT },
   { "ShiftLeft", RETROK_LSHIFT },
   { "ControlLeft", RETROK_LCTRL },
   { "AltLeft", RETROK_LALT },
   { "Space", RETROK_SPACE },
   { "Escape", RETROK_ESCAPE },
   { "NumpadAdd", RETROK_KP_PLUS },
   { "NumpadSubtract", RETROK_KP_MINUS },
   { "F1", RETROK_F1 },
   { "F2", RETROK_F2 },
   { "F3", RETROK_F3 },
   { "F4", RETROK_F4 },
   { "F5", RETROK_F5 },
   { "F6", RETROK_F6 },
   { "F7", RETROK_F7 },
   { "F8", RETROK_F8 },
   { "F9", RETROK_F9 },
   { "F10", RETROK_F10 },
   { "F11", RETROK_F11 },
   { "F12", RETROK_F12 },
   { "Digit0", RETROK_0 },
   { "Digit1", RETROK_1 },
   { "Digit2", RETROK_2 },
   { "Digit3", RETROK_3 },
   { "Digit4", RETROK_4 },
   { "Digit5", RETROK_5 },
   { "Digit6", RETROK_6 },
   { "Digit7", RETROK_7 },
   { "Digit8", RETROK_8 },
   { "Digit9", RETROK_9 },
   { "PageUp", RETROK_PAGEUP },
   { "PageDown", RETROK_PAGEDOWN },
   { "Numpad0", RETROK_KP0 },
   { "Numpad1", RETROK_KP1 },
   { "Numpad2", RETROK_KP2 },
   { "Numpad3", RETROK_KP3 },
   { "Numpad4", RETROK_KP4 },
   { "Numpad5", RETROK_KP5 },
   { "Numpad6", RETROK_KP6 },
   { "Numpad7", RETROK_KP7 },
   { "Numpad8", RETROK_KP8 },
   { "Numpad9", RETROK_KP9 },
   { "Period", RETROK_PERIOD },
   { "CapsLock", RETROK_CAPSLOCK },
   { "NumLock", RETROK_NUMLOCK },
   { "Backspace", RETROK_BACKSPACE },
   { "NumpadMultiply", RETROK_KP_MULTIPLY },
   { "NumpadDivide", RETROK_KP_DIVIDE },
   { "PrintScreen", RETROK_PRINT },
   { "ScrollLock", RETROK_SCROLLOCK },
   { "Backquote", RETROK_BACKQUOTE },
   { "Pause", RETROK_PAUSE },
   { "Quote", RETROK_QUOTE },
   { "Comma", RETROK_COMMA },
   { "Minus", RETROK_MINUS },
   { "Slash", RETROK_SLASH },
   { "Semicolon", RETROK_SEMICOLON },
   { "Equal", RETROK_EQUALS },
   { "BracketLeft", RETROK_LEFTBRACKET },
   { "Backslash", RETROK_BACKSLASH },
   { "BracketRight", RETROK_RIGHTBRACKET },
   { "NumpadDecimal", RETROK_KP_PERIOD },
   { "NumpadEqual", RETROK_KP_EQUALS },
   { "ControlRight", RETROK_RCTRL },
   { "AltRight", RETROK_RALT },
   { "F13", RETROK_F13 },
   { "F14", RETROK_F14 },
   { "F15", RETROK_F15 },
   { "MetaRight", RETROK_RMETA },
   { "MetaLeft", RETROK_LMETA },
   { "Help", RETROK_HELP },
   { "ContextMenu", RETROK_MENU },
   { "Power", RETROK_POWER },
};

/* to make the string labels for codes from JavaScript work, we convert them
 * to CRC32 hashes for the LUT */
static void rwebinput_generate_lut(void)
{
   int i;
   struct rarch_key_map *key_map;

   retro_assert(ARRAY_SIZE(rarch_key_map_rwebinput) ==
      ARRAY_SIZE(rwebinput_key_to_code_map) + 1);

   for (i = 0; i < ARRAY_SIZE(rwebinput_key_to_code_map); i++)
   {
      int j;
      uint32_t crc;
      const rwebinput_key_to_code_map_entry_t *key_to_code =
         &rwebinput_key_to_code_map[i];
      key_map = &rarch_key_map_rwebinput[i];
      crc = encoding_crc32(0, (const uint8_t *)key_to_code->key,
         strlen(key_to_code->key));

      /* sanity check: make sure there's no collisions */
      for (j = 0; j < i; j++)
         retro_assert(rarch_key_map_rwebinput[j].sym != crc);

      key_map->rk  = key_to_code->rk;
      key_map->sym = crc;
   }

   /* set terminating entry */
   key_map      = &rarch_key_map_rwebinput[
      ARRAY_SIZE(rarch_key_map_rwebinput) - 1];
   key_map->rk  = RETROK_UNKNOWN;
   key_map->sym = 0;
}
static EM_BOOL rwebinput_mouse_cb(int event_type,
   const EmscriptenMouseEvent *mouse_event, void *user_data)
{
   rwebinput_input_t *rwebinput      = (rwebinput_input_t*)user_data;

   uint8_t mask                      = 1 << mouse_event->button;

#ifdef WEB_SCALING
   double dpr = emscripten_get_device_pixel_ratio();
   rwebinput->mouse.x                = (long)(mouse_event->targetX * dpr);
   rwebinput->mouse.y                = (long)(mouse_event->targetY * dpr);
   rwebinput->mouse.pending_delta_x += (long)(mouse_event->movementX * dpr);
   rwebinput->mouse.pending_delta_y += (long)(mouse_event->movementY * dpr);
#else
   rwebinput->mouse.x                = mouse_event->targetX;
   rwebinput->mouse.y                = mouse_event->targetY;
   rwebinput->mouse.pending_delta_x += mouse_event->movementX;
   rwebinput->mouse.pending_delta_y += mouse_event->movementY;
#endif

   if (event_type ==  EMSCRIPTEN_EVENT_MOUSEDOWN)
      rwebinput->mouse.buttons |= mask;
   else if (event_type == EMSCRIPTEN_EVENT_MOUSEUP)
      rwebinput->mouse.buttons &= ~mask;

   return EM_FALSE;
}

static EM_BOOL rwebinput_wheel_cb(int event_type,
   const EmscriptenWheelEvent *wheel_event, void *user_data)
{
   rwebinput_input_t       *rwebinput = (rwebinput_input_t*)user_data;

#ifdef WEB_SCALING
   double dpr = emscripten_get_device_pixel_ratio();
   rwebinput->mouse.pending_scroll_x += wheel_event->deltaX * dpr;
   rwebinput->mouse.pending_scroll_y += wheel_event->deltaY * dpr;
#else
   rwebinput->mouse.pending_scroll_x += wheel_event->deltaX;
   rwebinput->mouse.pending_scroll_y += wheel_event->deltaY;
#endif

   return EM_TRUE;
}

static EM_BOOL rwebinput_touch_cb(int event_type,
   const EmscriptenTouchEvent *touch_event, void *user_data)
{
   rwebinput_input_t       *rwebinput = (rwebinput_input_t*)user_data;
   rwebinput_touch_t       *touch_handler = &rwebinput->touch;

   EmscriptenTouchPoint changed_touch;
   bool touch_changed = false;
   for (int i=0; i<touch_event->numTouches; i++) {
      if (touch_event->touches[i].isChanged) {
         changed_touch = touch_event->touches[i];
         touch_changed = true;
      }
   }
   if (!touch_changed) return EM_TRUE;
   if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART && touch_handler->last_touchdown_id != changed_touch.identifier) {
      touch_handler->clicked_yet = false;
      touch_handler->last_touchdown_id = changed_touch.identifier;
      touch_handler->last_touchdown_location = (changed_touch.canvasX + changed_touch.canvasY);
   }
   if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART && touch_handler->clicked_yet) {
      rwebinput->mouse.buttons |= 1 << 0;
   }
   if (event_type == EMSCRIPTEN_EVENT_TOUCHMOVE && touch_handler->last_touchdown_id == changed_touch.identifier) {
      long u = touch_handler->last_touchdown_location - (changed_touch.canvasX + changed_touch.canvasY);
      //25 may be too much of an offset...
      if (((u<0)?-u:u) > 25) {
         touch_handler->last_touchdown_id = -1;
      }
   }

   if (event_type == EMSCRIPTEN_EVENT_TOUCHCANCEL || event_type == EMSCRIPTEN_EVENT_TOUCHEND) {
      if (changed_touch.identifier == touch_handler->touch_id) {
         touch_handler->down = false;
      }
      if (touch_handler->last_touchdown_id == changed_touch.identifier && !touch_handler->clicked_yet) {
         touch_handler->clicked_yet = true;
      } else if (touch_handler->clicked_yet) {
         rwebinput->mouse.buttons &= ~(1 << 0);
         touch_handler->clicked_yet = false;
         touch_handler->last_touchdown_id = -1;
      }
      return EM_TRUE;
   } else if (touch_handler->down && changed_touch.identifier != touch_handler->touch_id) {
      return EM_TRUE; //I am not supporting multi touch
   }
   if (event_type == EMSCRIPTEN_EVENT_TOUCHSTART) {
      touch_handler->down = true;
      touch_handler->touch_id = changed_touch.identifier;
      touch_handler->last_canvasX = changed_touch.canvasX;
      touch_handler->last_canvasY = changed_touch.canvasY;
   } else if (event_type == EMSCRIPTEN_EVENT_TOUCHMOVE) {
      long diffX = changed_touch.canvasX - touch_handler->last_canvasX;
      long diffY = changed_touch.canvasY - touch_handler->last_canvasY;
      touch_handler->last_canvasX = changed_touch.canvasX;
      touch_handler->last_canvasY = changed_touch.canvasY;

#ifdef WEB_SCALING
      double dpr = emscripten_get_device_pixel_ratio();
      rwebinput->mouse.x                = (long)(changed_touch.canvasX * dpr);
      rwebinput->mouse.y                = (long)(changed_touch.canvasY * dpr);
      rwebinput->mouse.pending_delta_x += (long)(diffX * dpr);
      rwebinput->mouse.pending_delta_y += (long)(diffY * dpr);
#else
      rwebinput->mouse.x                = changed_touch.canvasX;
      rwebinput->mouse.y                = changed_touch.canvasY;
      rwebinput->mouse.pending_delta_x += diffX;
      rwebinput->mouse.pending_delta_y += diffY;
#endif

      //printf("diff: %li\n", diffX);
   }

   return EM_TRUE;
}

static void *rwebinput_input_init(const char *joypad_driver)
{
   EMSCRIPTEN_RESULT r;
   rwebinput_input_t *rwebinput =
      (rwebinput_input_t*)calloc(1, sizeof(*rwebinput));

   if (!rwebinput)
      return NULL;

   rwebinput_generate_lut();

   r = emscripten_set_mousedown_callback("#canvas", rwebinput, false, rwebinput_mouse_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create mousedown callback: %d\n", r);
   }

   r = emscripten_set_mouseup_callback("#canvas", rwebinput, false, rwebinput_mouse_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create mouseup callback: %d\n", r);
   }

   r = emscripten_set_mousemove_callback("#canvas", rwebinput, false, rwebinput_mouse_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create mousemove callback: %d\n", r);
   }

   r = emscripten_set_wheel_callback("#canvas", rwebinput, false, rwebinput_wheel_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create wheel callback: %d\n", r);
   }

   r = emscripten_set_touchstart_callback("#canvas", rwebinput, false, rwebinput_touch_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create wheel callback: %d\n", r);
   }

   r = emscripten_set_touchend_callback("#canvas", rwebinput, false, rwebinput_touch_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create wheel callback: %d\n", r);
   }

   r = emscripten_set_touchmove_callback("#canvas", rwebinput, false, rwebinput_touch_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create wheel callback: %d\n", r);
   }

   r = emscripten_set_touchcancel_callback("#canvas", rwebinput, false, rwebinput_touch_cb);
   if (r != EMSCRIPTEN_RESULT_SUCCESS)
   {
      RARCH_ERR(
         "[EMSCRIPTEN/INPUT] failed to create wheel callback: %d\n", r);
   }

   input_keymaps_init_keyboard_lut(rarch_key_map_rwebinput);

   return rwebinput;
}
static int16_t rwebinput_mouse_state(
      rwebinput_mouse_state_t *mouse,
      unsigned id, bool screen)
{
   switch (id)
   {
      case RETRO_DEVICE_ID_MOUSE_X:
         return (int16_t)(screen ? mouse->x : mouse->delta_x);
      case RETRO_DEVICE_ID_MOUSE_Y:
         return (int16_t)(screen ? mouse->y : mouse->delta_y);
      case RETRO_DEVICE_ID_MOUSE_LEFT:
         return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTNL));
      case RETRO_DEVICE_ID_MOUSE_RIGHT:
         return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTNR));
      case RETRO_DEVICE_ID_MOUSE_MIDDLE:
         return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTNM));
      case RETRO_DEVICE_ID_MOUSE_BUTTON_4:
         return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTN4));
      case RETRO_DEVICE_ID_MOUSE_BUTTON_5:
         return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTN5));
      case RETRO_DEVICE_ID_MOUSE_WHEELUP:
         return mouse->scroll_y < 0.0;
      case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
         return mouse->scroll_y > 0.0;
      case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP:
         return mouse->scroll_x < 0.0;
      case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN:
         return mouse->scroll_x > 0.0;
   }

   return 0;
}
struct rwebinput_code_to_key
{
   const int id;
   int down_1;
   int down_2;
   int down_3;
   int down_4;
};

static struct rwebinput_code_to_key stuff[] =
{
   { 0, 0, 0, 0, 0 }, //b
   { 1, 0, 0, 0, 0 }, //y
   { 2, 0, 0, 0, 0 }, //select
   { 3, 0, 0, 0, 0 }, //start
   { 4, 0, 0, 0, 0 }, //up
   { 5, 0, 0, 0, 0 }, //down
   { 6, 0, 0, 0, 0 }, //left
   { 7, 0, 0, 0, 0 }, //right
   { 8, 0, 0, 0, 0 }, //a
   { 9, 0, 0, 0, 0 }, //x
   { 10, 0, 0, 0, 0 }, //l
   { 11, 0, 0, 0, 0 }, //r
   { 12, 0, 0, 0, 0 }, //l2
   { 13, 0, 0, 0, 0 }, //r2
   { 14, 0, 0, 0, 0 }, //l3
   { 15, 0, 0, 0, 0 }, //r3
   { 16, 0, 0, 0, 0 }, //L STICK RIGHT
   { 17, 0, 0, 0, 0 }, //L STICK LEFT
   { 18, 0, 0, 0, 0 }, //L STICK DOWN
   { 19, 0, 0, 0, 0 }, //L STICK UP
   { 20, 0, 0, 0, 0 }, //R STICK RIGHT
   { 21, 0, 0, 0, 0 }, //R STICK LEFT
   { 22, 0, 0, 0, 0 }, //R STICK DOWN
   { 23, 0, 0, 0, 0 }, //R STICK UP
   { 24, 0, 0, 0, 0 },
   { 25, 0, 0, 0, 0 },
   { 26, 0, 0, 0, 0 },
   { 27, 0, 0, 0, 0 },
   { 28, 0, 0, 0, 0 },
};

void simulate_input(int user, int key, int down)
{
    int i;
    for (i=0; i<ARRAY_SIZE(stuff); i++) {
        if (stuff[i].id == key) {
            if (user == 0) {
                stuff[i].down_1 = down;
            } else if (user == 1) {
                stuff[i].down_2 = down;
            } else if (user == 2) {
                stuff[i].down_3 = down;
            } else if (user == 3) {
                stuff[i].down_4 = down;
            }
            break;
        }
    }
}

int is_pressed_hehe(int user, int id) {
    if (id >=24) return 0;
    for (int i=0; i<ARRAY_SIZE(stuff); i++) {
        if (stuff[i].id == id) {
            if (user == 0) {
                return stuff[i].down_1;
            } else if (user == 1) {
                return stuff[i].down_2;
            } else if (user == 2) {
                return stuff[i].down_3;
            } else if (user == 3) {
                return stuff[i].down_4;
            }
        }
    }
    return 0;
}


static int16_t rwebinput_input_state(
      void *data,
      const input_device_driver_t *joypad,
      const input_device_driver_t *sec_joypad,
      rarch_joypad_info_t *joypad_info,
      const retro_keybind_set *binds,
      bool keyboard_mapping_blocked,
      unsigned port,
      unsigned device,
      unsigned idx,
      unsigned id)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
         {
            unsigned i;
            int16_t ret = 0;
            for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
            {
                if (is_pressed_hehe(port, i))
                   ret |= (1 << i);
            }

            return ret;
         }

         if (id < RARCH_BIND_LIST_END)
         {
             if (is_pressed_hehe(port, id))
                return 1;
         }
         break;
      case RETRO_DEVICE_ANALOG: {
            unsigned id_minus     = 0;
            unsigned id_plus      = 0;
            int16_t ret           = 0;
            int rv = 0;

            input_conv_analog_id_to_bind_id(idx, id, id_minus, id_plus);


            rv = is_pressed_hehe(port, id_plus);
            if (rv)
               ret = rv;
            rv = is_pressed_hehe(port, id_minus);
            if (rv)
               ret -= rv;

            return ret;
         }
         break;
      case RETRO_DEVICE_KEYBOARD:
         return ((id < RETROK_LAST) && rwebinput->keys[id]);
      case RETRO_DEVICE_MOUSE:
      case RARCH_DEVICE_MOUSE_SCREEN:
         return rwebinput_mouse_state(&rwebinput->mouse, id,
               device == RARCH_DEVICE_MOUSE_SCREEN);
      case RETRO_DEVICE_POINTER:
      case RARCH_DEVICE_POINTER_SCREEN:
         if (idx == 0)
         {
            struct video_viewport vp;
            rwebinput_mouse_state_t
               *mouse                   = &rwebinput->mouse;
            const int edge_detect       = 32700;
            bool screen                 = device ==
               RARCH_DEVICE_POINTER_SCREEN;
            bool inside                 = false;
            int16_t res_x               = 0;
            int16_t res_y               = 0;
            int16_t res_screen_x        = 0;
            int16_t res_screen_y        = 0;

            vp.x                        = 0;
            vp.y                        = 0;
            vp.width                    = 0;
            vp.height                   = 0;
            vp.full_width               = 0;
            vp.full_height              = 0;

            if (!(video_driver_translate_coord_viewport_wrap(
                        &vp, mouse->x, mouse->y,
                        &res_x, &res_y, &res_screen_x, &res_screen_y)))
               return 0;

            if (screen)
            {
               res_x = res_screen_x;
               res_y = res_screen_y;
            }

            inside =    (res_x >= -edge_detect)
               && (res_y >= -edge_detect)
               && (res_x <= edge_detect)
               && (res_y <= edge_detect);

            switch (id)
            {
               case RETRO_DEVICE_ID_POINTER_X:
                  if (inside)
                     return res_x;
                  break;
               case RETRO_DEVICE_ID_POINTER_Y:
                  if (inside)
                     return res_y;
                  break;
               case RETRO_DEVICE_ID_POINTER_PRESSED:
                  return !!(mouse->buttons & (1 << RWEBINPUT_MOUSE_BTNL));
               case RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN:
                  return !inside;
               default:
                  break;
            }
         }
         break;
   }

   return 0;
}

static void rwebinput_input_free(void *data)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;

   emscripten_html5_remove_all_event_listeners();

   free(rwebinput->keyboard.events);

   free(data);
}

static void rwebinput_process_keyboard_events(
      rwebinput_input_t *rwebinput,
      rwebinput_keyboard_event_t *event)
{
   uint32_t keycode;
   unsigned translated_keycode;
   const EmscriptenKeyboardEvent *key_event = &event->event;
   bool keydown                             =
      event->type == EMSCRIPTEN_EVENT_KEYDOWN;

   keycode = encoding_crc32(0, (const uint8_t *)key_event->code,
      strnlen(key_event->code, sizeof(key_event->code)));
   translated_keycode = input_keymaps_translate_keysym_to_rk(keycode);


   if (     translated_keycode  < RETROK_LAST
         && translated_keycode != RETROK_UNKNOWN)
      rwebinput->keys[translated_keycode] = keydown;
}

static void rwebinput_input_poll(void *data)
{
   size_t i;
   rwebinput_input_t *rwebinput      = (rwebinput_input_t*)data;

   for (i = 0; i < rwebinput->keyboard.count; i++)
      rwebinput_process_keyboard_events(rwebinput,
         &rwebinput->keyboard.events[i]);

   rwebinput->keyboard.count         = 0;

   rwebinput->mouse.delta_x          = rwebinput->mouse.pending_delta_x;
   rwebinput->mouse.delta_y          = rwebinput->mouse.pending_delta_y;
   rwebinput->mouse.pending_delta_x  = 0;
   rwebinput->mouse.pending_delta_y  = 0;

   rwebinput->mouse.scroll_x         = rwebinput->mouse.pending_scroll_x;
   rwebinput->mouse.scroll_y         = rwebinput->mouse.pending_scroll_y;
   rwebinput->mouse.pending_scroll_x = 0;
   rwebinput->mouse.pending_scroll_y = 0;
}

static uint64_t rwebinput_get_capabilities(void *data)
{
   uint64_t caps = 0;

   caps |= (1 << RETRO_DEVICE_JOYPAD);
   caps |= (1 << RETRO_DEVICE_ANALOG);
   caps |= (1 << RETRO_DEVICE_KEYBOARD);
   caps |= (1 << RETRO_DEVICE_MOUSE);
   caps |= (1 << RETRO_DEVICE_POINTER);

   return caps;
}

input_driver_t input_emulatorjs = {
   rwebinput_input_init,
   rwebinput_input_poll,
   rwebinput_input_state,
   rwebinput_input_free,
   NULL,
   NULL,
   rwebinput_get_capabilities,
   "emulatorjs",
   NULL,
   NULL
};
