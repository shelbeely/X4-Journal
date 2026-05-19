#pragma once
#include "markdown_entry.h"

void screen_prompt_render(const char *prompt_text);
void screen_checkin_render_mood(int current_mood);
void screen_checkin_render_energy(int current_energy);
void screen_checkin_render_anxiety(int current_anxiety);
void screen_checkin_render_body(const char *current_feeling);
void screen_checkin_render_fragments(const char *const *frags, int count, int selected);
void screen_checkin_render_confirm(const journal_entry_t *e);
