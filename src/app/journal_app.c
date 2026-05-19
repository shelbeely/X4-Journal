#include "journal_app.h"
#include "journal_routes.h"
#include "entry_editor.h"
#include "timeline_view.h"
#include "prompt_engine.h"
#include "buttons.h"
#include "display.h"
#include "power.h"
#include "wifi.h"
#include "rtc.h"
#include "web_server.h"
#include "screen_home.h"
#include "screen_today.h"
#include "screen_timeline.h"
#include "screen_prompt.h"
#include "screen_settings.h"
#include "screen_sync.h"
#include "metadata_index.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "journal_app";

static QueueHandle_t   s_btn_queue;
static timeline_state_t s_timeline;

/* ---- Check-in flow state ---- */
typedef enum {
    CI_MOOD, CI_ENERGY, CI_ANXIETY, CI_BODY, CI_FEELING, CI_NEED, CI_WIN, CI_CONFIRM,
} checkin_step_t;

static checkin_step_t s_ci_step;
static int            s_ci_val;  /* current slider value */
static int            s_ci_frag; /* selected fragment index */

/* ---- Home menu ---- */
static int s_home_selected = 0;
#define HOME_ITEMS 6

static void btn_cb(button_event_t ev, void *ctx)
{
    xQueueSend(s_btn_queue, &ev, 0);
}

static void render_current_screen(void)
{
    switch (routes_current()) {
    case ROUTE_HOME:
        screen_home_render(s_home_selected);
        break;
    case ROUTE_TODAY:
        screen_today_render();
        break;
    case ROUTE_TIMELINE:
        screen_timeline_render(&s_timeline);
        break;
    case ROUTE_TIMELINE_ENTRY: {
        const entry_meta_t *m = timeline_get_selected(&s_timeline);
        if (m) {
            journal_entry_t e;
            if (entry_load(m->id, &e) == ESP_OK)
                screen_timeline_entry_render(&e);
        }
        break;
    }
    case ROUTE_CHECKIN:
        switch (s_ci_step) {
        case CI_MOOD:    screen_checkin_render_mood(s_ci_val); break;
        case CI_ENERGY:  screen_checkin_render_energy(s_ci_val); break;
        case CI_ANXIETY: screen_checkin_render_anxiety(s_ci_val); break;
        case CI_BODY:    screen_checkin_render_body(s_ci_val == 1 ? "good" :
                                                    s_ci_val == 2 ? "neutral" : "rough"); break;
        case CI_FEELING: screen_checkin_render_fragments(FEELING_FRAGMENTS,
                             FEELING_FRAGMENT_COUNT, s_ci_frag); break;
        case CI_NEED:    screen_checkin_render_fragments(NEED_FRAGMENTS,
                             NEED_FRAGMENT_COUNT, s_ci_frag); break;
        case CI_WIN:     screen_checkin_render_fragments(WIN_FRAGMENTS,
                             WIN_FRAGMENT_COUNT, s_ci_frag); break;
        case CI_CONFIRM: screen_checkin_render_confirm(entry_editor_get_current()); break;
        }
        break;
    case ROUTE_SYNC: {
        char ssid[32], ip[20];
        snprintf(ssid, sizeof(ssid), "PocketShrine");
        wifi_get_ap_ip(ip, sizeof(ip));
        screen_sync_render(ssid, ip);
        break;
    }
    case ROUTE_SETTINGS:
        screen_settings_render(0);
        break;
    default:
        screen_home_render(s_home_selected);
        break;
    }
}

/* ---- Route-specific button handlers ---- */

static void handle_home(button_event_t ev)
{
    if (ev.type == BTN_EVENT_RELEASE) {
        switch (ev.id) {
        case BTN_VOL_UP:
        case BTN_LEFT:
            if (s_home_selected > 0) s_home_selected--;
            render_current_screen();
            break;
        case BTN_VOL_DOWN:
        case BTN_RIGHT:
            if (s_home_selected < HOME_ITEMS - 1) s_home_selected++;
            render_current_screen();
            break;
        case BTN_CONFIRM:
            switch (s_home_selected) {
            case 0: /* New Entry */
                entry_editor_start(EDITOR_MODE_CHECKIN);
                s_ci_step = CI_MOOD;
                s_ci_val  = 3;
                routes_navigate(ROUTE_CHECKIN);
                break;
            case 1: /* Today */
                routes_navigate(ROUTE_TODAY);
                break;
            case 2: /* Timeline */
                timeline_load_recent(&s_timeline, 32);
                routes_navigate(ROUTE_TIMELINE);
                break;
            case 3: /* Prompts */
                routes_navigate(ROUTE_PROMPTS);
                break;
            case 4: /* Sync */
                wifi_ap_start(NULL);
                web_server_start();
                routes_navigate(ROUTE_SYNC);
                break;
            case 5: /* Settings */
                routes_navigate(ROUTE_SETTINGS);
                break;
            }
            render_current_screen();
            break;
        case BTN_POWER:
            display_sleep();
            power_sleep();
            display_wakeup();
            render_current_screen();
            break;
        default:
            break;
        }
    }
}

static void handle_timeline(button_event_t ev)
{
    if (ev.type != BTN_EVENT_RELEASE) return;
    switch (ev.id) {
    case BTN_VOL_DOWN:
    case BTN_RIGHT:
        timeline_scroll_down(&s_timeline);
        render_current_screen();
        break;
    case BTN_VOL_UP:
    case BTN_LEFT:
        timeline_scroll_up(&s_timeline);
        render_current_screen();
        break;
    case BTN_CONFIRM:
        routes_navigate(ROUTE_TIMELINE_ENTRY);
        render_current_screen();
        break;
    case BTN_BACK:
        routes_back();
        render_current_screen();
        break;
    default:
        break;
    }
}

static void handle_checkin(button_event_t ev)
{
    if (ev.type != BTN_EVENT_RELEASE && ev.type != BTN_EVENT_HOLD) return;

    if (ev.type == BTN_EVENT_HOLD && ev.id == BTN_CONFIRM) {
        /* hold confirm = save */
        if (s_ci_step == CI_CONFIRM) {
            char id[32];
            entry_editor_save(id, sizeof(id));
            routes_back();
            routes_back(); /* pop CHECKIN, back to HOME */
            render_current_screen();
        }
        return;
    }

    if (ev.type != BTN_EVENT_RELEASE) return;

    switch (ev.id) {
    case BTN_LEFT:
    case BTN_VOL_UP:
        if (s_ci_step <= CI_BODY) {
            if (s_ci_val > 1) s_ci_val--;
        } else {
            if (s_ci_frag > 0) s_ci_frag--;
        }
        render_current_screen();
        break;
    case BTN_RIGHT:
    case BTN_VOL_DOWN: {
        int max_val = (s_ci_step <= CI_ANXIETY) ? 5 :
                      (s_ci_step == CI_BODY)    ? 3 :
                      (s_ci_step == CI_FEELING) ? FEELING_FRAGMENT_COUNT - 1 :
                      (s_ci_step == CI_NEED)    ? NEED_FRAGMENT_COUNT - 1 :
                                                   WIN_FRAGMENT_COUNT - 1;
        if (s_ci_step <= CI_BODY) {
            if (s_ci_val < max_val) s_ci_val++;
        } else {
            if (s_ci_frag < max_val) s_ci_frag++;
        }
        render_current_screen();
        break;
    }
    case BTN_CONFIRM:
        switch (s_ci_step) {
        case CI_MOOD:
            entry_editor_set_mood(s_ci_val);
            s_ci_step = CI_ENERGY; s_ci_val = 3;
            break;
        case CI_ENERGY:
            entry_editor_set_energy(s_ci_val);
            s_ci_step = CI_ANXIETY; s_ci_val = 3;
            break;
        case CI_ANXIETY:
            entry_editor_set_anxiety(s_ci_val);
            s_ci_step = CI_BODY; s_ci_val = 2;
            break;
        case CI_BODY: {
            const char *feelings[] = {"", "good", "neutral", "rough"};
            entry_editor_set_body_feeling(feelings[s_ci_val]);
            s_ci_step = CI_FEELING; s_ci_frag = 0;
            break;
        }
        case CI_FEELING:
            entry_editor_add_fragment(FEELING_FRAGMENTS[s_ci_frag]);
            s_ci_step = CI_NEED; s_ci_frag = 0;
            break;
        case CI_NEED:
            entry_editor_add_fragment(NEED_FRAGMENTS[s_ci_frag]);
            s_ci_step = CI_WIN; s_ci_frag = 0;
            break;
        case CI_WIN:
            entry_editor_add_fragment(WIN_FRAGMENTS[s_ci_frag]);
            s_ci_step = CI_CONFIRM;
            break;
        case CI_CONFIRM: {
            char id[32];
            entry_editor_save(id, sizeof(id));
            routes_back();
            render_current_screen();
            return;
        }
        }
        render_current_screen();
        break;
    case BTN_BACK:
        if (s_ci_step == CI_MOOD) {
            entry_editor_discard();
            routes_back();
        } else {
            s_ci_step = (checkin_step_t)((int)s_ci_step - 1);
        }
        render_current_screen();
        break;
    default:
        break;
    }
}

static void handle_sync(button_event_t ev)
{
    if (ev.type == BTN_EVENT_RELEASE && ev.id == BTN_BACK) {
        web_server_stop();
        wifi_ap_stop();
        routes_back();
        render_current_screen();
    }
}

static void handle_generic_back(button_event_t ev)
{
    if (ev.type == BTN_EVENT_RELEASE && ev.id == BTN_BACK) {
        routes_back();
        render_current_screen();
    }
}

esp_err_t journal_app_init(void)
{
    s_btn_queue = xQueueCreate(16, sizeof(button_event_t));
    if (!s_btn_queue) return ESP_ERR_NO_MEM;
    timeline_init(&s_timeline);
    return ESP_OK;
}

void journal_app_task(void *arg)
{
    (void)arg;
    routes_init();
    entry_editor_init();
    render_current_screen();

    button_event_t ev;
    while (1) {
        if (xQueueReceive(s_btn_queue, &ev, portMAX_DELAY)) {
            switch (routes_current()) {
            case ROUTE_HOME:           handle_home(ev);     break;
            case ROUTE_TIMELINE:
            case ROUTE_TIMELINE_ENTRY: handle_timeline(ev); break;
            case ROUTE_CHECKIN:        handle_checkin(ev);  break;
            case ROUTE_SYNC:           handle_sync(ev);     break;
            default:                   handle_generic_back(ev); break;
            }
        }
    }
}
