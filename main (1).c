// MINI ARCADE - PSP homebrew (pspdev). 8 oyun, 100 animasyonlu arka plan, muzik.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

PSP_MODULE_INFO("MiniArcade", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define STRIDE 512
#define RGB(r,g,b) (0xFF000000u|((u32)(b)<<16)|((u32)(g)<<8)|(u32)(r))
static u32 *vram = (u32 *)0x44000000;
static volatile int running = 1, music_on = 1;
static SceCtrlData pad;
static u32 prev, pressed, held;
static int tick, anim;
static const int DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};

/* ---------- cizim ---------- */
static void rect(int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 480) w = 480 - x;
    if (y + h > 272) h = 272 - y;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) vram[(y + j) * STRIDE + x + i] = c;
}
static void disc(int cx, int cy, int r, u32 c) {
    for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
        if (i * i + j * j <= r * r) rect(cx + i, cy + j, 1, 1, c);
}
static void line(int x0, int y0, int x1, int y1, int th, u32 c) {
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (!n) n = 1;
    for (int i = 0; i <= n; i++) rect(x0 + (x1 - x0) * i / n - th / 2, y0 + (y1 - y0) * i / n - th / 2, th, th, c);
}
static void txt(int cx, int cy, u32 c, const char *f, ...) {
    char b[64]; va_list a; va_start(a, f); vsnprintf(b, 64, f, a); va_end(a);
    pspDebugScreenSetXY(cx, cy); pspDebugScreenSetTextColor(c); pspDebugScreenPuts(b);
}

/* ---------- 100 animasyon = 10 sekil x 10 renk paleti ---------- */
static const float PAL[10][3] = {{1,.2f,.4f},{.2f,.5f,1},{.3f,1,.4f},{1,.8f,.1f},{.7f,.3f,1},
                                 {.1f,.9f,.9f},{1,.5f,.1f},{.9f,.9f,.9f},{1,.1f,.1f},{.4f,.6f,.2f}};
static float fv(int s, int bx, int by, float t) {
    float x = bx, y = by;
    switch (s) {
    case 0: return 0.5f + 0.5f * sinf(y * 0.4f + t);
    case 1: return 0.5f + 0.5f * sinf(x * 0.3f - t);
    case 2: return 0.5f + 0.5f * sinf((x + y) * 0.25f + t);
    case 3: return ((((int)(x + t * 3)) >> 2) + (((int)y) >> 2)) & 1 ? 0.8f : 0.2f;
    case 4: { float dx = x - 30, dy = y - 17; return 0.5f + 0.5f * sinf(sqrtf(dx * dx + dy * dy) * 0.5f - t * 2); }
    case 5: return 0.5f + 0.5f * sinf(x * 0.2f + t) * cosf(y * 0.3f - t);
    case 6: return ((bx * 7 + by * 13 + (int)(t * 5) * (bx % 3 + 1)) % 29 == 0) ? 1.f : 0.1f;
    case 7: return 0.5f + 0.5f * sinf(x * 0.15f + sinf(y * 0.3f + t) * 2);
    case 8: return 0.5f + 0.5f * sinf(t) * (1 - y / 34.f);
    default: return 0.5f + 0.25f * (sinf(x * 0.2f + t) + sinf(y * 0.25f - t));
    }
}
static void draw_bg(void) {
    int s = anim % 10, p = anim / 10; float t = tick * 0.04f;
    for (int by = 0; by < 34; by++) for (int bx = 0; bx < 60; bx++) {
        float v = fv(s, bx, by, t) * 0.4f * 255;
        rect(bx * 8, by * 8, 8, 8, RGB((int)(PAL[p][0] * v), (int)(PAL[p][1] * v), (int)(PAL[p][2] * v)));
    }
}
static void frame_begin(void) {
    sceDisplayWaitVblankStart();
    sceCtrlPeekBufferPositive(&pad, 1);
    held = pad.Buttons; pressed = held & ~prev; prev = held; tick++;
    if (pressed & PSP_CTRL_LTRIGGER) anim = (anim + 99) % 100;
    if (pressed & PSP_CTRL_RTRIGGER) anim = (anim + 1) % 100;
    if (pressed & PSP_CTRL_TRIANGLE) music_on = !music_on;
    draw_bg();
}
#define BEGIN frame_begin(); if ((pressed & PSP_CTRL_CIRCLE) || !running) return;

/* ---------- muzik (kare dalga) ---------- */
static int audio_thread(SceSize a, void *b) {
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_MONO);
    static short buf[1024];
    static const int mel[2][16] = {
        {523,659,784,659,523,392,440,494,523,659,784,1047,988,784,659,523},
        {392,0,392,494,587,0,587,494,440,523,659,523,440,392,330,392}};
    int n = 0, k = 0, ph = 0, song = 0;
    while (running) {
        int f = mel[song][n & 15], amp = 3000 - k * 300;
        for (int i = 0; i < 1024; i++) {
            ph = (ph + f) % 44100;
            buf[i] = (music_on && f) ? ((ph < 22050) ? amp : -amp) : 0;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
        if (++k >= 8) { k = 0; n++; if ((n & 31) == 0) song ^= 1; }
    }
    return 0;
}

/* ---------- 1) PACMAN ---------- */
static const char *MZ[15] = {
"#####################","#.........#.........#","#.###.###.#.###.###.#","#...................#",
"#.###.#.#####.#.###.#","#.....#...#...#.....#","#####.###.#.###.#####","#.....#.......#.....#",
"#####.###.#.###.#####","#.....#...#...#.....#","#.###.#.#####.#.###.#","#...................#",
"#.###.###.#.###.###.#","#.........#.........#","#####################"};
static void pac_pos(int *px, int *py, int *pd, int *nd, int *gx, int *gy) {
    *px = 10; *py = 3; *pd = 0; *nd = 0;
    gx[0] = 10; gy[0] = 7; gx[1] = 1; gy[1] = 13; gx[2] = 19; gy[2] = 13;
}
static void g_pac(void) {
    char m[15][22]; int px, py, pd, nd, gx[3], gy[3], gd[3] = {0, 0, 0}, dots = 0, sc = 0, lives = 3, cnt = 0, st = 0;
    for (;;) { BEGIN
        if (st == 0 || (st == 2 && (pressed & PSP_CTRL_CROSS))) {
            dots = 0;
            for (int r = 0; r < 15; r++) { for (int c = 0; c < 21; c++) { m[r][c] = MZ[r][c]; if (m[r][c] == '.') dots++; } m[r][21] = 0; }
            pac_pos(&px, &py, &pd, &nd, gx, gy); sc = 0; lives = 3; st = 1;
        }
        if (st == 1) {
            if (pressed & PSP_CTRL_RIGHT) nd = 0; if (pressed & PSP_CTRL_DOWN) nd = 1;
            if (pressed & PSP_CTRL_LEFT) nd = 2;  if (pressed & PSP_CTRL_UP) nd = 3;
            if (++cnt >= 7) {
                cnt = 0;
                if (m[py + DY[nd]][px + DX[nd]] != '#') pd = nd;
                int nx = px + DX[pd], ny = py + DY[pd];
                if (m[ny][nx] != '#') { px = nx; py = ny; }
                if (m[py][px] == '.') { m[py][px] = ' '; sc++; dots--; }
                for (int i = 0; i < 3; i++) {
                    int best = -1, bd = 9999;
                    for (int d = 0; d < 4; d++) {
                        if (d == (gd[i] + 2) % 4) continue;
                        int ax = gx[i] + DX[d], ay = gy[i] + DY[d];
                        if (m[ay][ax] == '#') continue;
                        int dist = abs(ax - px) + abs(ay - py);
                        if (rand() % 100 < 35) dist = rand() % 50;
                        if (dist < bd) { bd = dist; best = d; }
                    }
                    if (best < 0) best = (gd[i] + 2) % 4;
                    gd[i] = best; gx[i] += DX[best]; gy[i] += DY[best];
                }
                for (int i = 0; i < 3; i++) if (gx[i] == px && gy[i] == py) {
                    lives--; pac_pos(&px, &py, &pd, &nd, gx, gy); if (lives <= 0) st = 2; break;
                }
                if (dots <= 0) st = 2;
            }
        }
        int x0 = 72, y0 = 16;
        for (int r = 0; r < 15; r++) for (int c = 0; c < 21; c++) {
            if (m[r][c] == '#') rect(x0 + c * 16 + 1, y0 + r * 16 + 1, 14, 14, RGB(40, 60, 255));
            else if (m[r][c] == '.') rect(x0 + c * 16 + 6, y0 + r * 16 + 6, 4, 4, RGB(255, 230, 180));
        }
        disc(x0 + px * 16 + 8, y0 + py * 16 + 8, 7, RGB(255, 255, 0));
        static const u32 GC[3] = {RGB(255,0,0), RGB(255,120,200), RGB(0,230,230)};
        for (int i = 0; i < 3; i++) disc(x0 + gx[i] * 16 + 8, y0 + gy[i] * 16 + 8, 7, GC[i]);
        txt(0, 1, RGB(255,255,255), "SKOR"); txt(0, 2, RGB(255,255,0), "%d", sc);
        txt(0, 4, RGB(255,255,255), "CAN"); txt(0, 5, RGB(255,80,80), "%d", lives);
        if (st == 2) txt(22, 16, RGB(255,255,255), dots <= 0 ? " KAZANDIN! X=tekrar " : " OYUN BITTI X=tekrar ");
    }
}

/* ---------- 2) YILAN ---------- */
static void g_snake(void) {
    int sx[600], sy[600], len = 0, dir = 0, fx = 20, fy = 8, dead = 1, sc = 0, cnt = 0;
    for (;;) { BEGIN
        if (dead && (pressed & PSP_CTRL_CROSS)) {
            len = 3; dir = 0; for (int i = 0; i < 3; i++) { sx[i] = 10 - i; sy[i] = 8; }
            fx = 20; fy = 8; dead = 0; sc = 0; cnt = 0;
        }
        if (!dead) {
            int nd = dir;
            if (pressed & PSP_CTRL_RIGHT) nd = 0; if (pressed & PSP_CTRL_DOWN) nd = 1;
            if (pressed & PSP_CTRL_LEFT) nd = 2;  if (pressed & PSP_CTRL_UP) nd = 3;
            if (nd != (dir + 2) % 4) dir = nd;
            int sp = 8 - sc / 4; if (sp < 3) sp = 3;
            if (++cnt >= sp) {
                cnt = 0; int tx = sx[len - 1], ty = sy[len - 1];
                for (int i = len - 1; i > 0; i--) { sx[i] = sx[i - 1]; sy[i] = sy[i - 1]; }
                sx[0] += DX[dir]; sy[0] += DY[dir];
                if (sx[0] < 0 || sx[0] >= 30 || sy[0] < 0 || sy[0] >= 17) dead = 1;
                for (int i = 1; i < len; i++) if (sx[i] == sx[0] && sy[i] == sy[0]) dead = 1;
                if (!dead && sx[0] == fx && sy[0] == fy) {
                    sx[len] = tx; sy[len] = ty; len++; sc++; fx = rand() % 30; fy = rand() % 17;
                }
            }
        }
        rect(fx * 16 + 2, fy * 16 + 2, 12, 12, RGB(255, 60, 60));
        for (int i = 0; i < len; i++) rect(sx[i] * 16 + 1, sy[i] * 16 + 1, 14, 14, i ? RGB(60, 220, 60) : RGB(180, 255, 80));
        txt(1, 0, RGB(255,255,255), "SKOR %d", sc);
        if (dead) txt(16, 16, RGB(255,255,255), " X = basla ");
    }
}

/* ---------- 3) XOX ---------- */
static int xo_win(int *b) {
    static const int L[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for (int i = 0; i < 8; i++) if (b[L[i][0]] && b[L[i][0]] == b[L[i][1]] && b[L[i][1]] == b[L[i][2]]) return b[L[i][0]];
    for (int i = 0; i < 9; i++) if (!b[i]) return 0;
    return 3;
}
static void g_xox(void) {
    int b[9] = {0}, cur = 4, res = 0;
    for (;;) { BEGIN
        if ((pressed & PSP_CTRL_LEFT) && cur % 3) cur--;
        if ((pressed & PSP_CTRL_RIGHT) && cur % 3 < 2) cur++;
        if ((pressed & PSP_CTRL_UP) && cur > 2) cur -= 3;
        if ((pressed & PSP_CTRL_DOWN) && cur < 6) cur += 3;
        if (pressed & PSP_CTRL_CROSS) {
            if (res) { memset(b, 0, sizeof b); res = 0; }
            else if (!b[cur]) {
                b[cur] = 1; res = xo_win(b);
                if (!res) {
                    int m = -1;
                    for (int p = 2; p >= 1 && m < 0; p--) for (int i = 0; i < 9 && m < 0; i++)
                        if (!b[i]) { b[i] = p; if (xo_win(b) == p) m = i; b[i] = 0; }
                    if (m < 0 && !b[4]) m = 4;
                    while (m < 0) { int r = rand() % 9; if (!b[r]) m = r; }
                    b[m] = 2; res = xo_win(b);
                }
            }
        }
        int x0 = 144, y0 = 40;
        rect(x0 + (cur % 3) * 64, y0 + (cur / 3) * 64, 64, 64, RGB(80, 80, 140));
        for (int i = 1; i < 3; i++) { rect(x0 + i * 64 - 2, y0, 4, 192, RGB(255,255,255)); rect(x0, y0 + i * 64 - 2, 192, 4, RGB(255,255,255)); }
        for (int i = 0; i < 9; i++) {
            int cx = x0 + (i % 3) * 64 + 32, cy = y0 + (i / 3) * 64 + 32;
            if (b[i] == 1) { line(cx - 20, cy - 20, cx + 20, cy + 20, 5, RGB(255,80,80)); line(cx + 20, cy - 20, cx - 20, cy + 20, 5, RGB(255,80,80)); }
            if (b[i] == 2) { disc(cx, cy, 22, RGB(80,200,255)); disc(cx, cy, 16, RGB(20,20,50)); }
        }
        txt(2, 2, RGB(255,255,255), "XOX  (sen=X)");
        if (res) txt(21, 30, RGB(255,255,0), res == 1 ? " KAZANDIN! " : res == 2 ? " BILGISAYAR KAZANDI " : " BERABERE ");
    }
}

/* ---------- 4) PONG ---------- */
static void g_pong(void) {
    float bx = 240, by = 136, vx = 3, vy = 2, py = 110, ay = 110; int sp = 0, sa = 0;
    for (;;) { BEGIN
        if (held & PSP_CTRL_UP) py -= 4; if (held & PSP_CTRL_DOWN) py += 4;
        if (py < 0) py = 0; if (py > 222) py = 222;
        ay += (by - (ay + 25)) * 0.07f; if (ay < 0) ay = 0; if (ay > 222) ay = 222;
        bx += vx; by += vy;
        if (by < 4 || by > 268) vy = -vy;
        if (bx < 20 && bx > 10 && by > py && by < py + 50 && vx < 0) { vx = -vx * 1.05f; vy += (by - py - 25) * 0.1f; }
        if (bx > 460 && bx < 470 && by > ay && by < ay + 50 && vx > 0) vx = -vx;
        if (vx > 8) vx = 8; if (vx < -8) vx = -8;
        if (bx < 0 || bx > 480) { if (bx < 0) sa++; else sp++; bx = 240; by = 136; vx = (rand() & 1) ? 3 : -3; vy = 2; }
        rect(10, (int)py, 8, 50, RGB(255,255,255)); rect(462, (int)ay, 8, 50, RGB(255,255,255));
        disc((int)bx, (int)by, 5, RGB(255,255,0));
        txt(25, 1, RGB(255,255,255), "%d : %d", sp, sa);
    }
}

/* ---------- 5) BREAKOUT ---------- */
static void fill_bricks(int br[5][10]) { for (int r = 0; r < 5; r++) for (int c = 0; c < 10; c++) br[r][c] = 1; }
static void g_break(void) {
    int br[5][10], alive = 50, lives = 3, sc = 0; float px = 200, bx = 240, by = 200, vx = 3, vy = -3;
    fill_bricks(br);
    for (;;) { BEGIN
        if (held & PSP_CTRL_LEFT) px -= 6; if (held & PSP_CTRL_RIGHT) px += 6;
        if (px < 0) px = 0; if (px > 400) px = 400;
        bx += vx; by += vy;
        if (bx < 3 || bx > 477) vx = -vx; if (by < 3) vy = -vy;
        if (by > 250 && by < 258 && bx > px && bx < px + 80 && vy > 0) { vy = -vy; vx = (bx - px - 40) * 0.1f; }
        int r = ((int)by - 30) / 14, c = (int)bx / 48;
        if (by >= 30 && r >= 0 && r < 5 && c >= 0 && c < 10 && br[r][c]) { br[r][c] = 0; vy = -vy; sc++; alive--; }
        if (by > 272) { lives--; bx = 240; by = 200; vx = 3; vy = -3; if (lives <= 0) { lives = 3; sc = 0; alive = 50; fill_bricks(br); } }
        if (alive == 0) { alive = 50; fill_bricks(br); }
        for (r = 0; r < 5; r++) for (c = 0; c < 10; c++) if (br[r][c])
            rect(c * 48 + 2, 30 + r * 14, 44, 12, RGB(255 - r * 40, 80 + r * 30, 60 + r * 40));
        rect((int)px, 255, 80, 8, RGB(255,255,255)); disc((int)bx, (int)by, 4, RGB(255,255,0));
        txt(1, 0, RGB(255,255,255), "SKOR %d  CAN %d", sc, lives);
    }
}

/* ---------- 6) FLAPPY ---------- */
static void g_flappy(void) {
    float y = 130, vy = 0; int px[3] = {999, 999, 999}, gy[3] = {100, 100, 100}, dead = 1, sc = 0;
    for (;;) { BEGIN
        if (dead && (pressed & PSP_CTRL_CROSS)) {
            y = 130; vy = 0; sc = 0; dead = 0;
            for (int i = 0; i < 3; i++) { px[i] = 300 + i * 160; gy[i] = 60 + rand() % 120; }
        } else if (!dead) {
            if (pressed & PSP_CTRL_CROSS) vy = -5;
            vy += 0.4f; y += vy;
            for (int i = 0; i < 3; i++) {
                px[i] -= 3;
                if (px[i] < -40) { px[i] += 480; gy[i] = 60 + rand() % 120; sc++; }
                if (px[i] < 108 && px[i] + 40 > 92 && (y < gy[i] - 45 || y > gy[i] + 45)) dead = 1;
            }
            if (y < 0 || y > 272) dead = 1;
        }
        for (int i = 0; i < 3; i++) if (px[i] < 480) {
            rect(px[i], 0, 40, gy[i] - 45, RGB(40,200,60)); rect(px[i], gy[i] + 45, 40, 272, RGB(40,200,60));
        }
        disc(100, (int)y, 8, RGB(255,230,0));
        txt(1, 0, RGB(255,255,255), "SKOR %d", sc);
        if (dead) txt(16, 16, RGB(255,255,255), " X = ucur ");
    }
}

/* ---------- 7) CATCH ---------- */
static void item_reset(float *x, float *y) { *x = rand() % 460 + 10; *y = -(rand() % 200); }
static void g_catch(void) {
    float px = 200, ix[5], iy[5]; int lives = 3, sc = 0;
    for (int i = 0; i < 5; i++) item_reset(&ix[i], &iy[i]);
    for (;;) { BEGIN
        if (lives <= 0) { if (pressed & PSP_CTRL_CROSS) { lives = 3; sc = 0; } }
        else {
            if (held & PSP_CTRL_LEFT) px -= 6; if (held & PSP_CTRL_RIGHT) px += 6;
            if (px < 0) px = 0; if (px > 420) px = 420;
            for (int i = 0; i < 5; i++) {
                iy[i] += 2 + sc * 0.05f;
                if (iy[i] > 245 && iy[i] < 262 && ix[i] > px - 8 && ix[i] < px + 68) { sc++; item_reset(&ix[i], &iy[i]); }
                else if (iy[i] > 272) { lives--; item_reset(&ix[i], &iy[i]); }
            }
        }
        for (int i = 0; i < 5; i++) disc((int)ix[i], (int)iy[i], 7, RGB(255,60,60));
        rect((int)px, 255, 60, 10, RGB(255,255,255));
        txt(1, 0, RGB(255,255,255), "SKOR %d  CAN %d", sc, lives);
        if (lives <= 0) txt(16, 16, RGB(255,255,255), " OYUN BITTI X=tekrar ");
    }
}

/* ---------- 8) SHOOTER ---------- */
static void g_shoot(void) {
    float sx = 230, bx[8], by[8], ex[8], ey[8]; int ba[8] = {0}, lives = 3, sc = 0, cd = 0;
    for (int i = 0; i < 8; i++) item_reset(&ex[i], &ey[i]);
    for (;;) { BEGIN
        if (lives <= 0) { if (pressed & PSP_CTRL_CROSS) { lives = 3; sc = 0; } }
        else {
            if (held & PSP_CTRL_LEFT) sx -= 5; if (held & PSP_CTRL_RIGHT) sx += 5;
            if (sx < 0) sx = 0; if (sx > 460) sx = 460;
            if (cd > 0) cd--;
            if ((held & PSP_CTRL_CROSS) && cd == 0) {
                for (int i = 0; i < 8; i++) if (!ba[i]) { ba[i] = 1; bx[i] = sx + 10; by[i] = 245; cd = 10; break; }
            }
            for (int i = 0; i < 8; i++) if (ba[i]) { by[i] -= 6; if (by[i] < 0) ba[i] = 0; }
            for (int i = 0; i < 8; i++) {
                ey[i] += 1 + sc * 0.03f;
                for (int j = 0; j < 8; j++) if (ba[j] && fabsf(bx[j] - ex[i]) < 12 && fabsf(by[j] - ey[i]) < 12) { ba[j] = 0; sc++; item_reset(&ex[i], &ey[i]); break; }
                if (ey[i] > 262) { lives--; item_reset(&ex[i], &ey[i]); }
            }
        }
        rect((int)sx, 250, 20, 12, RGB(80,200,255));
        for (int i = 0; i < 8; i++) { if (ba[i]) rect((int)bx[i], (int)by[i], 3, 8, RGB(255,255,0)); rect((int)ex[i] - 8, (int)ey[i] - 8, 16, 16, RGB(255,60,200)); }
        txt(1, 0, RGB(255,255,255), "SKOR %d  CAN %d", sc, lives);
        if (lives <= 0) txt(16, 16, RGB(255,255,255), " OYUN BITTI X=tekrar ");
    }
}

/* ---------- MENU ---------- */
static int exit_cb(int a, int b, void *c) { running = 0; sceKernelExitGame(); return 0; }
static int cb_thread(SceSize a, void *b) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id); sceKernelSleepThreadCB(); return 0;
}
int main(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL); if (t >= 0) sceKernelStartThread(t, 0, NULL);
    pspDebugScreenInit(); pspDebugScreenEnableBackColor(0);
    sceCtrlSetSamplingCycle(0); sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    t = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, 0, NULL); if (t >= 0) sceKernelStartThread(t, 0, NULL);
    static const char *names[8] = {"PACMAN", "YILAN", "XOX", "PONG", "BREAKOUT", "FLAPPY", "CATCH", "SHOOTER"};
    static void (*games[8])(void) = {g_pac, g_snake, g_xox, g_pong, g_break, g_flappy, g_catch, g_shoot};
    int sel = 0;
    while (running) {
        frame_begin();
        if ((pressed & PSP_CTRL_UP) && sel > 0) sel--;
        if ((pressed & PSP_CTRL_DOWN) && sel < 7) sel++;
        if (pressed & PSP_CTRL_CROSS) { games[sel](); continue; }
        rect(120, 20, 240, 200, RGB(10, 10, 30));
        int pulse = 128 + (int)(127 * sinf(tick * 0.1f));
        txt(19, 4, RGB(255, pulse, 0), "=== MINI ARCADE ===");
        for (int i = 0; i < 8; i++) txt(24, 8 + i * 2, i == sel ? RGB(255,255,0) : RGB(200,200,200), "%s %s", i == sel ? ">" : " ", names[i]);
        txt(1, 32, RGB(255,255,255), "L/R: ANIM %d/100   X: oyna   O: cikis   TRI: muzik", anim + 1);
    }
    sceKernelExitGame();
    return 0;
}
