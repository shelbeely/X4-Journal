#include "screen_prompt.h"
#include "components.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

void screen_prompt_render(const char *prompt_text)
{
    display_clear();
    ui_draw_header("Prompt");
    ui_draw_wrapped_text(4, 22, X4_DISPLAY_WIDTH - 8, prompt_text ? prompt_text : "");
    ui_draw_status_bar(0, "");
    display_full_refresh();
}

static void render_slider(const char *label, int value, int max_val)
{
    display_clear();
    ui_draw_header(label);
    int mid_y = 80;
    ui_draw_progress_bar(10, mid_y, X4_DISPLAY_WIDTH - 20, 20, value, max_val);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    ui_draw_centered_text(mid_y + 26, buf);
    ui_draw_centered_text(mid_y + 40, "< > change  OK confirm");
    display_full_refresh();
}

void screen_checkin_render_mood(int current_mood)
{
    render_slider("Mood (1-5)", current_mood, 5);
}

void screen_checkin_render_energy(int current_energy)
{
    render_slider("Energy (1-5)", current_energy, 5);
}

void screen_checkin_render_anxiety(int current_anxiety)
{
    render_slider("Anxiety (1-5)", current_anxiety, 5);
}

void screen_checkin_render_body(const char *current_feeling)
{
    display_clear();
    ui_draw_header("Body feeling?");
    static const char *opts[] = {"good", "neutral", "rough"};
    for (int i = 0; i < 3; i++) {
        bool sel = (current_feeling && strcmp(current_feeling, opts[i]) == 0);
        ui_draw_menu_item(40 + i * 32, opts[i], sel);
    }
    display_full_refresh();
}

void screen_checkin_render_fragments(const char *const *frags, int count, int selected)
{
    display_clear();
    ui_draw_header("Pick a phrase");
    int visible = 6;
    int scroll  = selected > visible - 1 ? selected - visible + 1 : 0;
    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        if (idx >= count) break;
        ui_draw_menu_item(20 + i * 28, frags[idx], idx == selected);
    }
    display_full_refresh();
}

void screen_checkin_render_confirm(const journal_entry_t *e)
{
    display_clear();
    ui_draw_header("Save entry?");
    if (!e) { display_full_refresh(); return; }

    char line[64];
    snprintf(line, sizeof(line), "Mood:%d  Energy:%d  Anxiety:%d",
             e->mood, e->energy, e->anxiety);
    display_draw_text(2, 22, line, 1);

    if (e->body[0]) {
        ui_draw_wrapped_text(2, 36, X4_DISPLAY_WIDTH - 4, e->body);
    }

    ui_draw_centered_text(X4_DISPLAY_HEIGHT - 30, "CONFIRM = save");
    ui_draw_centered_text(X4_DISPLAY_HEIGHT - 20, "BACK = edit");
    display_full_refresh();
}
