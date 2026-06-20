/*
 * jarvis_ui_tokens.h — JARVIS AI-OS framebuffer HUD design tokens
 * ----------------------------------------------------------------------------
 * Phase 4 / goal #2. Single source of truth for the on-screen palette and
 * field geometry. These centralize the constants currently defined inline in
 * main_x86.c:1212-1223 — replace that block with `#include "jarvis_ui_tokens.h"`
 * so there is ONE definition, not a parallel set.
 *
 * Render constraints (do not violate): 1024x768x32, BGRX byte order, no R<->B
 * swap (fb_set_rb_swap(0)); one 8x16 mono bitmap font => 128 cols x 48 rows;
 * native primitives only (fb_fill_rect / fb_draw_text / 1px lines / bars).
 * No alpha on the framebuffer — any "tint" is a precomputed solid hex.
 *
 * Tag key per constant:
 *   [LIVE] — drawn by the deployed build today (cited).
 *   [ASPIRATIONAL] — reserved for a widget with no live data source yet.
 */
#ifndef JARVIS_UI_TOKENS_H
#define JARVIS_UI_TOKENS_H

#include <stdint.h>

/* ============================================================================
 * 1. PALETTE — 0x00RRGGBB. BGRX framebuffer; fb_pack() stores correctly.
 * ==========================================================================*/

/* ---- FBP_* : the live status-panel palette (main_x86.c:1213-1217) --------- */
#define FBP_BG        0x0A0B0Eu   /* [LIVE] canvas / panel background          */
#define FBP_FG        0xC8D0E0u   /* [LIVE] light-gray primary text            */
#define FBP_ACCENT    0x4F7CFFu   /* [LIVE] electric blue — title / in-progress*/
#define FBP_OK        0x3FB950u   /* [LIVE] green — loaded / PASS / err=0       */
#define FBP_ERR       0xF2564Bu   /* [LIVE] red   — err>0 / alerts              */

/* ---- JCLR_* : extended ramp (token port). Mostly aspirational on-panel ----
 * The deployed panel uses only FBP_* above. These are reserved for the 2c-2
 * log-styling + aspirational widgets; keep them here so there is one ramp.    */
#define JCLR_WARN     0xD6A02Au   /* [ASPIRATIONAL] amber — SHIELD/monitoring, warnings */
#define JCLR_LIVE     0x46C7C7u   /* [ASPIRATIONAL] cyan — streaming/live role           */
#define JCLR_MUTED    0x6B7589u   /* [LIVE*] muted labels in the log region              */
#define JCLR_TEXT2    0x98A2B6u   /* [LIVE*] secondary text in the log region            */
#define JCLR_CARD     0x12151Bu   /* [ASPIRATIONAL] solid "surface" (tint precomputed)   */
#define JCLR_HOVER    0x1D222Cu   /* [ASPIRATIONAL] inset fill                            */
#define JCLR_LINE     0x272D39u   /* [LIVE*] 1px hairline divider (fb_fill_rect 1px)      */
/* *LIVE once the 2c-2 scrolling-log region ships; today only FBP_* are drawn. */

/* ============================================================================
 * 2. GEOMETRY — 8x16 cell grid.  px = (col*8, row*16).
 * ==========================================================================*/
#define JUI_CELL_W     8u
#define JUI_CELL_H     16u
#define JUI_COLS       128u       /* 1024 / 8  */
#define JUI_ROWS       48u        /*  768 / 16 */

/* ---- Live shipped panel field positions (main_x86.c:1212,1218-1223) -------
 * NOTE: the shipped panel is pixel-Y addressed, not all on the 16px grid
 * (24px field pitch from y=48). Kept EXACTLY as the deployed build draws them.*/
#define FBP_X          16u        /* [LIVE] left margin (col 2)                 */
#define FBP_Y_TITLE    16u        /* [LIVE] "JARVIS AI-OS ..."        FBP_ACCENT*/
#define FBP_Y_DISPLAY  48u        /* [LIVE] "Display : 1024x768x32 ..."  FBP_FG */
#define FBP_Y_MODEL    72u        /* [LIVE] "Model   : ... [loading N%]/[loaded]"*/
#define FBP_Y_THREADS  96u        /* [LIVE] "Threads : NUM_NODES=N ..."  FBP_FG */
#define FBP_Y_QUERIES 120u        /* [LIVE] "Queries : q=.. err=.."  OK / ERR   */
#define FBP_Y_LAST    144u        /* [LIVE] "Last    : <response>"       FBP_FG */
/* self-test line ("Self-test 5/5 PASS", FBP_OK) is live-backed; place it on a
 * free row below FBP_Y_LAST when adding it as a persistent field.            */

/* ---- Aspirational 2c-2 expanded-view positions (col,row; NOT yet drawn) ----
 * Provided so the expanded HUD can be laid out against one grid. Each needs
 * the data plumbing noted before it can leave [ASPIRATIONAL].                 */
#define JUI_LOG_COL        2u     /* event-log region origin (col)             */
#define JUI_LOG_ROW       14u     /* needs: 2c-2 scrolling nvme_log renderer   */
#define JUI_LOG_W_COLS   124u
#define JUI_LOG_H_ROWS    26u
#define JUI_COUNTERS_ROW  43u     /* [LIVE data] q/hits/infer/hb/shield/err strip */
#define JUI_ROUTE_ROW     10u     /* [LIVE data] CACHE-HIT vs ->LLM (cache_lookup) */
#define JUI_CPU_COL       80u     /* [ASPIRATIONAL] per-core meter — no source */
#define JUI_AGENTS_ROW     8u     /* [ASPIRATIONAL] per-agent — not in deploy loop */
#define JUI_SHIELD_ROW    13u     /* [ASPIRATIONAL] risk meter — runtime echoes ALLOW */

/* ============================================================================
 * 3. STATE -> ACCENT  (header status badge color)
 * ==========================================================================*/
#define JUI_C_BOOTING  FBP_ACCENT /* [LIVE] booting / loading                  */
#define JUI_C_READY    FBP_OK     /* [LIVE] model resident, err=0              */
#define JUI_C_ACTIVE   JCLR_LIVE  /* [ASPIRATIONAL] cyan "active" accent       */
#define JUI_C_ERROR    FBP_ERR    /* [LIVE] err>0                              */

/* ----------------------------------------------------------------------------
 * Honesty (must hold wherever these tokens render):
 *   - never print "formally verified";
 *   - SHIELD is monitoring/passive (runtime returns ALLOW, inference_server.c:668-673);
 *   - tok/s is offline-only (no live on-panel readout);
 *   - if a version string appears, it is v0.2.1-beta (banner prints -dev, main_x86.c:2387);
 *   - no emoji — status is a colored bullet + UPPERCASE badge.
 * --------------------------------------------------------------------------*/
#endif /* JARVIS_UI_TOKENS_H */
