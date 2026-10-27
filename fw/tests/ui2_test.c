/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* INSTRUMENTS and the EDITOR on the PC (fw/core with a fake platform, as ui_test.c): the demo disk laid
 * into a fake flash, the screens' states the playground's tests/playground/screens.js shoots, shot the
 * same (PPM, compared by ui_shots.js), and edits made through the controls, saved, read back after a
 * remount: each checked here, and the pattern and cartridge written out for ui2_check.py, which unpacks
 * them with the packer's own code and packs them again byte for byte.
 *
 *   ui2_test DISK.ptfmdsk OUTDIR      OUTDIR/<shot>.ppm, the records in OUTDIR/edited/, a line a check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "app.h"
#include "store/store.h"
#include "store/crc.h"
#include "ui/ui_core.h"
#include "ui/ui_priv.h"
#include "ui/ui_data.h"
#include "ui/ui_ed.h"
#include "ui/ed_pat.h"
#include "ui/ed_inst.h"
#include "ui/gfx.h"
#include "play/rec.h"

/* ------------------------------------------------------------- the fake FM-1 --- */
static uint8_t flash[1 << 20];
static uint16_t screen[240 * 240];
static uint32_t now_ms;
static plat_input_t in_now;
static int fails;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_us(void) { return now_ms * 1000u; }
void plat_input(plat_input_t *in) { *in = in_now; memset(in_now.knob, 0, sizeof in_now.knob); }
uint32_t plat_notes(void) { return in_now.notes; }
void plat_led(int id, uint8_t level) { (void)id; (void)level; }
void plat_lcd_rows(int y, int h, const uint16_t *px) { memcpy(screen + y * 240, px, (size_t)h * 240 * 2); }
int plat_screen_lost(void) { return 0; }
void plat_cpu(plat_cpu_t *c, int reset) { (void)reset; memset(c, 0, sizeof *c); c->half_us = 5804; c->c2_state = PLAT_C2_OFF; }
void plat_output(int mono, int line_gain_db) { (void)mono; (void)line_gain_db; }
void plat_usb_gain(int db) { (void)db; }
static int user(uint32_t a, uint32_t n)
{
    return (a >= STORE_U1_START && a + n <= STORE_U1_END) || (a >= STORE_U2_START && a + n <= STORE_U2_END);
}
const uint8_t *plat_flash_ptr(uint32_t a) { return user(a, 1) ? flash + a : 0; }
int plat_flash_read(uint32_t a, void *d, uint32_t n) { if (a + n > sizeof flash) return -1; memcpy(d, flash + a, n); return 0; }
int plat_flash_erase(uint32_t a) { if (a & 4095 || a >= sizeof flash) return -1; memset(flash + a, 0xFF, 4096); return 0; }
int plat_flash_prog(uint32_t a, const void *s, uint32_t n)
{
    if (n > 256 || (a & ~255u) != ((a + n - 1) & ~255u)) return -1;
    for (uint32_t i = 0; i < n; i++) flash[a + i] &= ((const uint8_t *)s)[i];
    return 0;
}
uint32_t plat_fw_end(void) { return 0x50000; }
int plat_sysex_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
int plat_battery(void) { return 50; }
void plat_idle(void) {}
uint32_t plat_lock(void) { return 0; }
void plat_unlock(uint32_t was) { (void)was; }
void plat_fault(const char *why) { fprintf(stderr, "FAULT: %s\n", why); exit(2); }

/* ------------------------------------------------ the disk laid into the flash --- */
static uint32_t at = STORE_U1_START, last_hdr;
static void put(const uint8_t *h4, const uint8_t *payload, uint32_t len, int audio)
{
    uint32_t size = 16 + len;
    if (!audio && (at & 4095) + size > 4096) at = (at + 4095) & ~4095u;
    uint8_t hd[16];
    memcpy(hd, h4, 4);
    hd[4] = (uint8_t)len; hd[5] = (uint8_t)(len >> 8); hd[6] = (uint8_t)(len >> 16);
    hd[7] = 0;
    hd[8] = 1; hd[9] = hd[10] = hd[11] = 0;             /* save 1 */
    memcpy(flash + at, hd, 12);
    memcpy(flash + at + 16, payload, len);
    last_hdr = at;
    at = (at + size + 3) & ~3u;
}
static void seal(uint32_t a)                            /* a header's CRC */
{
    uint32_t len = flash[a + 4] | flash[a + 5] << 8 | flash[a + 6] << 16;
    uint32_t c = crc32_update(0, flash + a, 12);
    c = crc32_update(c, flash + a + 16, len);
    flash[a + 12] = (uint8_t)c; flash[a + 13] = (uint8_t)(c >> 8); flash[a + 14] = (uint8_t)(c >> 16); flash[a + 15] = (uint8_t)(c >> 24);
}
static void lay_disk(const char *path)
{
    memset(flash, 0xFF, sizeof flash);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ui2_test: no %s\n", path); exit(1); }
    static uint8_t d[1 << 20];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 16 || memcmp(d, "PTFMDISK", 8)) { fprintf(stderr, "ui2_test: %s is no disk\n", path); exit(1); }
    uint32_t count = d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24, p;
    static uint32_t hdrs[512];
    int nh = 0;
    for (int pass = 0; pass < 2; pass++) {              /* records, then audio */
        if (pass) at = (at + 4095) & ~4095u;
        p = 16;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = d[p + 4] | d[p + 5] << 8 | d[p + 6] << 16 | (uint32_t)d[p + 7] << 24;
            int kind = d[p], audio = kind == REC_AUDIO;
            if (kind < 0xE0 && audio == pass) { put(d + p, d + p + 12, len, audio); hdrs[nh++] = last_hdr; }
            p = (p + 12 + len + 3) & ~3u;
        }
    }
    flash[last_hdr + 7] |= 1;                           /* the save's last record */
    for (int i = 0; i < nh; i++) seal(hdrs[i]);
}

/* --------------------------------------------------------------- the script --- */
static const char *out;
static void run(uint32_t ms)
{
    static int16_t lr[PLAT_HALF * 2];
    static uint32_t audio_at;
    for (uint32_t t = 0; t < ms; t++) {
        now_ms++;
        while (audio_at * 5804u / 1000u < now_ms) { app_audio(lr, PLAT_HALF); audio_at++; }
        app_poll();
    }
}
static void press(int b) { in_now.buttons |= 1u << b; run(30); }
static void release(int b) { in_now.buttons &= ~(1u << b); run(30); }
static void tap(int b) { press(b); release(b); run(200); }
static void key(int n, int down) { if (down) in_now.notes |= 1u << n; else in_now.notes &= ~(1u << n); run(30); }
static void keytap(int n) { key(n, 1); key(n, 0); run(100); }
static void turn(int k, int clicks)
{
    for (int i = 0; i < (clicks < 0 ? -clicks : clicks); i++) { in_now.knob[k] += clicks < 0 ? -1 : 1; run(120); }
}
static void combo(int held, int b) { press(held); tap(b); release(held); run(200); }
static void home_key(int n) { press(B_HOME); key(n, 1); key(n, 0); release(B_HOME); run(200); }
static void shot(const char *name)
{
    run(40);
    ui_render();                                        /* (a frame now: the main loop's last can be 33 ms old) */
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", out, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (int i = 0; i < 240 * 240; i++) {
        uint32_t c = screen[i];
        fputc((int)((c >> 11) * 255u / 31u), f);
        fputc((int)(((c >> 5) & 63u) * 255u / 63u), f);
        fputc((int)((c & 31u) * 255u / 31u), f);
    }
    fclose(f);
    static uint16_t parts[240 * 240];                   /* the screen as sent in parts == the frame whole */
    memcpy(parts, screen, sizeof parts);
    g_invalidate();
    ui_render();
    if (memcmp(parts, screen, sizeof parts)) { printf("FAIL %s: the parts sent differ from the whole frame\n", name); fails++; }
    else printf("shot %s: partial redraws == the whole frame\n", name);
}
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}
static int same_cell(const rec_cell_t *a, const rec_cell_t *b)
{
    if (a->has != b->has) return 0;
    if ((a->has & RC_NOTE) && a->note != b->note) return 0;
    if ((a->has & RC_INS) && (a->cart != b->cart || a->slot != b->slot)) return 0;
    if ((a->has & RC_VOL) && a->vol != b->vol) return 0;
    if ((a->has & RC_CMD) && (a->n != b->n || memcmp(a->cmd, b->cmd, (size_t)a->n * 2))) return 0;
    return 1;
}
static void dump(const char *name, uint8_t kind, uint8_t num, int subs)
{
    char path[512];
    for (int s = 0; s < subs; s++) {
        uint32_t len;
        uint8_t ver;
        const uint8_t *p = store_get(kind, num, (uint8_t)s, &len, &ver);
        if (!p) continue;
        snprintf(path, sizeof path, "%s/edited/%s_%d.bin", out, name, s);
        FILE *f = fopen(path, "wb");
        if (!f) { perror(path); exit(1); }
        fwrite(p, 1, len, f);
        fclose(f);
    }
}
/* the first cartridge of a kind the user can edit; a built-in one cloned into the user area first, as
 * the playground's test does */
static int first_of(int kind)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++) if (nums[i] < 0xF0 && ins_kind(nums[i]) == kind) return nums[i];
    for (int i = 0; i < n; i++)
        if (ins_kind(nums[i]) == kind) {
            rec_cart_t c;
            char nm[16];
            int to = ins_free_cart();
            rec_cart(nums[i], &c);
            rec_cart_name(&c, nm);
            ins_copy_cart(nums[i], (uint8_t)to, nm);
            return to;
        }
    return -1;
}
static int first_empty(int cart)
{
    uint8_t s[112];
    for (int k = 0; k < 64; k++) if (!ins_slot_bytes((uint8_t)cart, k, s)) return k;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: ui2_test DISK.ptfmdsk OUTDIR\n"); return 1; }
    out = argv[2];
    lay_disk(argv[1]);
    app_init();
    run(100);

    /* ---- the EDITOR: the cell of the pool with the most commands (as the playground's test) ---- */
    uint8_t pats[256];
    int np = store_list(REC_PATTERN, pats, 256), best_n = -1, bp = 0, br = 0, bt = 0;
    for (int i = 0; i < np; i++) {
        rec_pat_t p;
        if (rec_pattern(pats[i], &p)) continue;
        for (int r = 0; r < p.rows; r++)
            for (int t = 0; t < p.ntrk; t++) {
                rec_cell_t c;
                if (ed_cell(pats[i], r, t, &c) && (c.has & RC_CMD) && c.n > best_n) { best_n = c.n; bp = pats[i]; br = r; bt = t; }
            }
    }
    ed_editor_goto((uint8_t)bp, br, bt);
    home_key(3);
    shot("05_editor8");
    tap(B_SEQ); run(2100); shot("06_editor4");
    tap(B_SEQ); run(2100); shot("07_editor2");
    turn(K_K1, 1); run(1300); shot("07b_editor2_cmd2");
    turn(K_K1, -2); run(1300); shot("07c_editor2_vol");
    turn(K_K1, 1);
    tap(B_SEQ); run(2100); shot("07d_editor1");
    tap(B_SEQ); run(2100);
    tap(B_EDIT); run(1300); shot("08_note_editor"); tap(B_EDIT);
    combo(B_HOME, B_LFO); shot("09_help_editor"); tap(B_HOME);
    tap(B_ENV); shot("10_pattern_list"); tap(B_HOME);
    press(B_SEL); run(20); turn(K_SELECT, 3); turn(K_ALG, 2); release(B_SEL); run(150);
    shot("11_block"); tap(B_SEL); shot("12_block_menu"); tap(B_HOME); tap(B_HOME);
    check(ui_unsaved == 0, "looking changes nothing");

    /* ---- INSTRUMENTS ---- */
    int dx = first_of(REC_DX7), ju = first_of(REC_JUNO), tb = first_of(REC_TB303), fu = first_of(REC_FUSION), sa = first_of(REC_SAMPLER);
    check(dx >= 0 && ju >= 0 && tb >= 0 && fu >= 0 && sa >= 0, "a cartridge of each kind to edit");
    check(dx != ju && ju != tb && tb != fu && fu != dx, "the clones under numbers of their own (an edited cartridge's "
          "number is taken)");
    home_key(5);
    ui_set_inst((uint16_t)(dx << 8)); run(50); shot("13_instr_dx7");
    tap(B_EDIT); shot("14_instr_dx7_edit");
    turn(K_ALG, 8); tap(B_SEL); shot("14b_name_entry"); tap(B_HOME); tap(B_EDIT);
    ui_set_inst((uint16_t)(ju << 8)); run(50); shot("15_instr_juno");
    tap(B_EDIT); shot("16_instr_juno_edit"); tap(B_EDIT);
    ui_set_inst((uint16_t)(tb << 8)); run(50); shot("17_instr_tb303");
    tap(B_EDIT); shot("17b_instr_tb303_edit"); tap(B_EDIT);
    ui_set_inst(0xF900); run(50); shot("17c_instr_chip");          /* (CHIPTUNE, built in: looked at, not changed) */
    tap(B_EDIT); shot("17d_instr_chip_edit"); turn(K_ALG, 2); shot("17e_instr_chip_tables");
    turn(K_ALG, 1); shot("17f_instr_chip_wave"); tap(B_EDIT);
    ui_set_inst((uint16_t)(sa << 8)); run(50); shot("18_instr_sampler");
    tap(B_EDIT); shot("19_instr_sampler_edit"); tap(B_SEQ); shot("19b_instr_sampler_env"); tap(B_EDIT);
    ui_set_inst((uint16_t)(fu << 8)); run(50); shot("20_instr_fusion");
    tap(B_EDIT); shot("20b_instr_fusion_edit");
    turn(K_SELECT, 2); tap(B_SEL); shot("20bb_fusion_menu"); tap(B_HOME);
    turn(K_SELECT, -2); tap(B_SEL); run(50); shot("20c_fusion_editor");
    tap(B_HOME); tap(B_HOME);
    ui_set_inst(0x0100);
    ui_set_inst((uint16_t)(fu << 8 | first_empty(fu)));
    tap(B_EDIT); shot("20d_fusion_fill"); tap(B_HOME);
    tap(B_ENV); shot("21_cartridges"); tap(B_HOME);
    /* the clones are edits: saved, so what follows starts from a clean state */
    tap(B_SAVE); run(3000);
    check(ui_unsaved == 0, "the clones saved");

    /* ---- editing a pattern: REC and the keys, LFO, K1-K4; SAVE; read back after a remount ---- */
    ui_song_t sg;
    ui_song(ui_cur.song, &sg);
    uint8_t P = sg.order[0];
    rec_cell_t before[4], want[4];
    static const int R[4] = {0, 1, 4, 8}, Tk[4] = {0, 0, 1, 0};
    for (int i = 0; i < 4; i++) ed_cell(P, R[i], Tk[i], &before[i]);
    ui_set_inst((uint16_t)(dx << 8));
    ed_editor_goto(P, 0, 0);
    home_key(3);
    tap(B_REC);
    keytap(0);                                          /* the note at row 0, track 0; the cursor a row on */
    int m = ui_midi(0);
    ed_cell(P, 0, 0, &want[0]);
    check((want[0].has & RC_NOTE) && want[0].note == m && (want[0].has & RC_INS) && want[0].cart == dx && want[0].slot == 0 &&
          (want[0].has & RC_VOL) && want[0].vol == ui_set[S_KEYS_VOLUME], "REC + a key writes its note, the instrument and KEYS VOLUME");
    check(!(before[0].has & RC_CMD) || ((want[0].has & RC_CMD) && want[0].n == before[0].n), "the cell's commands stay");
    ed_editor_goto(P, 1, 0);
    tap(B_LFO);                                         /* row 1: cleared, or a note off on an empty cell */
    ed_cell(P, 1, 0, &want[1]);
    {
        int was_empty = !before[1].has;
        check(was_empty ? (want[1].has == RC_NOTE && want[1].note == REC_NOTE_OFF) : !want[1].has, "LFO clears a cell, or writes a note off on an empty one");
    }
    tap(B_REC);                                         /* writing off: K1-K4 still edit */
    ed_editor_goto(P, 4, 1);
    turn(K_K1, -4);                                     /* the volume */
    turn(K_K4, -3);                                     /* past 7F: 7F, 7E, 7D */
    ed_cell(P, 4, 1, &want[2]);
    check((want[2].has & RC_VOL) && want[2].vol == 0x7D, "K4 on the volume: by 1 from past 7F");
    turn(K_K1, 4);                                      /* one past the last command: to add one */
    turn(K_K2, 1);                                      /* the first entry: ARPEGGIO */
    turn(K_K3, 2); turn(K_K4, 5);
    ed_cell(P, 4, 1, &want[2]);
    {
        int found = 0;
        for (int k = 0; k < want[2].n; k++) if (want[2].cmd[k][0] == 0x00 && want[2].cmd[k][1] == 0x25) found = 1;
        check((want[2].has & RC_CMD) && found, "K2 adds a command, K3 and K4 set its digits (025)");
    }
    /* the note editor: HOME drops what changed since EDIT */
    ed_editor_goto(P, 8, 0);
    tap(B_EDIT);
    turn(K_SELECT, -8);                                 /* NOTE */
    turn(K_K1, 3);
    ed_cell(P, 8, 0, &want[3]);
    check(!same_cell(&want[3], &before[3]), "the note editor's K1 changes the note");
    tap(B_HOME);
    ed_cell(P, 8, 0, &want[3]);
    check(same_cell(&want[3], &before[3]), "HOME closes the note editor, the cell as it was");
    /* REVERT (specs/ui/saving.md#revert), on another pattern than P's (whose edits are saved below): a note written,
     * then the CELL menu's REVERT, its last line while the pattern is unsaved, after its question */
    uint8_t Q = P;
    {
        uint8_t nums[64];
        int np = store_list(REC_PATTERN, nums, 64);
        for (int i = 0; i < np && Q == P; i++) if (nums[i] != P && nums[i] < 0xF0) Q = nums[i];
    }
    check(Q != P, "another pattern to try REVERT on");
    rec_cell_t u0, u1;
    ed_cell(Q, 2, 3, &u0);
    ed_editor_goto(Q, 2, 3);
    tap(B_REC); keytap(2); tap(B_REC);
    ed_cell(Q, 2, 3, &u1);
    check(!same_cell(&u0, &u1) && ui_edit_unsaved(REC_PATTERN, Q, 0), "a note written: the pattern unsaved");
    tap(B_SEL); turn(K_SELECT, 30); tap(B_SEL);         /* REVERT */
    check(ui_top() && ui_top()->kind == OV_QUESTION, "REVERT asks first");
    tap(B_OCTP);                                        /* REVERT */
    ed_cell(Q, 2, 3, &u1);
    check(same_cell(&u0, &u1) && !ui_edit_unsaved(REC_PATTERN, Q, 0), "REVERT: the pattern as saved, the cell back");
    /* a realtime take: REC on, PLAY: the loop from the cursor's row; a key lands on the row playing, its
     * release a note off on the next; PLAY again stops it, the cursor back on the take's row (the stand-in
     * player stays on its row, so both land where the take started) */
    {
        rec_cell_t a, b, a0, b0;
        ed_cell(Q, 20, 2, &a0);
        ed_cell(Q, 21, 2, &b0);
        ed_editor_goto(Q, 20, 2);
        tap(B_REC);
        tap(B_PLAY);
        key(4, 1); run(100); key(4, 0); run(100);
        tap(B_PLAY);
        tap(B_REC);
        ed_cell(Q, 20, 2, &a);
        ed_cell(Q, 21, 2, &b);
        check((a.has & RC_NOTE) && a.note == ui_midi(4), "a take writes a key's note on the row playing");
        check((b.has & RC_NOTE) && b.note == REC_NOTE_OFF, "its release writes a note off, never on the note's own row");
        check(ui_edit_revert(REC_PATTERN, Q, 0) == 0, "REVERT of the pattern");
        ed_cell(Q, 20, 2, &a);
        ed_cell(Q, 21, 2, &b);
        check(same_cell(&a, &a0) && same_cell(&b, &b0), "REVERT: the take's cells as they were");
    }
    ed_editor_goto(P, 0, 0);
    /* SAVE, a remount: the edits read back from the flash */
    rec_cell_t got[4];
    for (int i = 0; i < 3; i++) ed_cell(P, R[i], Tk[i], &want[i]);
    tap(B_SAVE); run(3000);
    check(ui_unsaved == 0, "SAVE: nothing unsaved");
    store_mount();
    for (int i = 0; i < 3; i++) { ed_cell(P, R[i], Tk[i], &got[i]); }
    check(same_cell(&got[0], &want[0]) && same_cell(&got[1], &want[1]) && same_cell(&got[2], &want[2]), "after a remount, the edited cells read back");
    dump("pattern", REC_PATTERN, P, 16);
    {
        char path[512];
        snprintf(path, sizeof path, "%s/edited/pattern.txt", out);
        FILE *f = fopen(path, "w");
        fprintf(f, "%d\n", P);
        for (int i = 0; i < 3; i++) fprintf(f, "%d %d\n", R[i], Tk[i]);
        fclose(f);
    }

    /* ---- editing an instrument: EDIT, K1 on OP1's LEVEL; SAVE; read back ---- */
    uint8_t s0[112], s1[112];
    ins_slot_bytes((uint8_t)dx, 0, s0);
    home_key(5);
    ui_set_inst((uint16_t)(dx << 8));
    tap(B_EDIT);
    turn(K_K1, -5);                                     /* OP1 LEVEL down by 5 */
    tap(B_EDIT);
    ins_slot_bytes((uint8_t)dx, 0, s1);
    const ins_page_t *pg;
    ins_pages(REC_DX7, &pg);
    int l0 = ins_get(s0, &pg[0].g[0].p[0]), l1 = ins_get(s1, &pg[0].g[0].p[0]);
    check(l1 == (l0 >= 5 ? l0 - 5 : 0), "K1 in edit mode: OP1 LEVEL by 1 a click");
    /* HOME in edit mode drops the changes */
    tap(B_EDIT); turn(K_K2, 3); tap(B_HOME);
    ins_slot_bytes((uint8_t)dx, 0, s0);
    check(!memcmp(s0, s1, 112), "HOME leaves edit mode, the instrument as it was");
    tap(B_SAVE); run(3000);
    store_mount();
    ins_slot_bytes((uint8_t)dx, 0, s0);
    check(!memcmp(s0, s1, 112), "after a remount, the edited instrument reads back");
    dump("cartridge", REC_DX7, (uint8_t)dx, 1);
    {
        char path[512];
        snprintf(path, sizeof path, "%s/edited/cartridge.txt", out);
        FILE *f = fopen(path, "w");
        fprintf(f, "%d %d %d\n", dx, l1, REC_DX7);
        fclose(f);
    }

    /* ---- a block: SEL held + SELECT / ALG marks, the menu's COPY, PASTE elsewhere ---- */
    home_key(3);
    ed_editor_goto(P, 16, 0);
    press(B_SEL); run(20); turn(K_SELECT, 3); turn(K_ALG, 1); release(B_SEL); run(150);
    tap(B_SEL); turn(K_SELECT, -20); tap(B_SEL);       /* the block's menu: COPY first */
    check(ed_clip_rows() == 4 && ed_clip_tracks() == 2, "COPY: a block of 4 x 2");
    ed_editor_goto(P, 32, 0);
    tap(B_SEL); turn(K_SELECT, -20); turn(K_SELECT, 4); tap(B_SEL);     /* MARK, TAKE ITS INSTRUMENT, COPY, CUT, PASTE */
    int ok = 1;
    for (int r = 0; r < 4; r++)
        for (int t = 0; t < 2; t++) {
            rec_cell_t a, b;
            ed_cell(P, 16 + r, t, &a);
            ed_cell(P, 32 + r, t, &b);
            if (!same_cell(&a, &b)) ok = 0;
        }
    check(ok, "PASTE: the block's cells at the cursor");
    /* GLO: fewer rows asks, YES drops what lies past them */
    int rows0 = ui_pattern_rows(P);
    tap(B_GLO); turn(K_SELECT, 1); turn(K_K1, -1);
    shot("ui2_rows_question");
    tap(B_OCTP); tap(B_GLO);
    check(ui_pattern_rows(P) == rows0 - 1, "GLO's ROWS: one row less (asked first when cells are dropped)");
    /* the pattern list's NEW */
    int free0 = ed_first_free();
    tap(B_ENV); tap(B_SEL); turn(K_SELECT, -20); tap(B_SEL); run(100);
    check(free0 >= 0 && ui_pattern_rows((uint8_t)free0) == 64 && ui_pattern_tracks((uint8_t)free0) == 8, "the pattern list's NEW: 8 x 64 at the first free number");
    tap(B_HOME);
    /* the clipboard's 2 KB (ED_CLIP), each track's 0x00 included: track 0 128 cells of 13 B (note, volume, 4
     * commands) + 0x00 = 1,665 B, track 1 29 such + one of 5 B (1 command) + 0x00 = 383 B: 2,048 B, it just fits;
     * with an empty track 2 after them it does not (and nothing is written past the clipboard) */
    int big = ed_first_free();
    rec_cell_t fc = {RC_NOTE | RC_VOL | RC_CMD, 48, 0, 0, 0x40, 4, {{1, 1}, {2, 2}, {3, 3}, {4, 4}}};
    ok = big >= 0 && !ed_new_pattern((uint8_t)big, 3, 128, 1);
    for (int r = 0; r < 128 && ok; r++) ok = !ed_put_cell((uint8_t)big, r, 0, &fc);
    for (int r = 0; r < 29 && ok; r++) ok = !ed_put_cell((uint8_t)big, r, 1, &fc);
    fc.n = 1;
    ok = ok && !ed_put_cell((uint8_t)big, 29, 1, &fc);
    check(ok && !ed_clip_copy((uint8_t)big, 0, 127, 0, 1) && ed_clip_rows() == 128 && ed_clip_tracks() == 2,
          "the clipboard: a block of 2,048 B packed is copied");
    check(ed_clip_copy((uint8_t)big, 0, 127, 0, 2) == -1 && !ed_clip_rows(), "... one more track (empty): too big, the clipboard empty");
    /* a new FUSION pattern is first offered filled with the last sound picked that is not a FUSION slot (not the
     * first sound there is: screens/instruments.md); FILL writes its note on row 3C */
    home_key(5);
    ui_set_inst((uint16_t)(dx << 8 | 3));
    ui_set_inst((uint16_t)(fu << 8 | first_empty(fu)));
    int fp = ed_first_free();
    tap(B_EDIT);
    const char *w = ui_top() ? ui_overlay_words(ui_top()) : 0;
    if (w && strcmp(w, "A NEW PATTERN FOR THIS SLOT")) { tap(B_OCTP); run(50); w = ui_top() ? ui_overlay_words(ui_top()) : 0; }  /* (the EDITOR's own pattern's warning first) */
    check(w && !strcmp(w, "A NEW PATTERN FOR THIS SLOT"), "EDIT on an empty FUSION slot: FILL's question");
    tap(B_OCTP); run(100);
    rec_cell_t fcell;
    check(fp >= 0 && ed_cell((uint8_t)fp, 0x3C, 0, &fcell) && (fcell.has & RC_INS) && fcell.cart == dx && fcell.slot == 3,
          "... it offered the last sound picked (the DX7's slot 03), FILL wrote it on row 3C");

    printf(fails ? "ui2_test: %d FAILED\n" : "ui2_test: all passed\n", fails);
    return fails ? 1 : 0;
}
