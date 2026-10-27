/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Forced first into the UI tests' packed builds (-include): ui_fmt and ui_toastf checked as printf, so a table's
 * text number (an int in the packed build) given to `%s` without T() is a compile error (specs/ui/texts.md#tests) */
int ui_fmt(char *dst, int max, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void ui_toastf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
