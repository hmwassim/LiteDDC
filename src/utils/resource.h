#pragma once

#define IDI_APP 101
// M14 themed tray glyphs (simple sun): black for light taskbars, white for
// dark taskbars; swapped live on WM_SETTINGCHANGE("ImmersiveColorSet").
#define IDI_APP_SUN_LIGHT 102
#define IDI_APP_SUN_DARK 103

#define IDD_SETTINGS 203

#define IDC_STATIC -1

// SettingsDialog controls (M4). Row pattern per parameter:
// checkbox gates the SCROLL gesture only; trackbar + numeric label are the
// always-usable slider pair (unless the parameter is unsupported on the
// selected monitor scope - then the whole row grays out).
#define IDC_CHK_BRIGHTNESS 210
#define IDC_TRK_BRIGHTNESS 211
#define IDC_LBL_BRIGHTNESS 212
#define IDC_CHK_CONTRAST 213
#define IDC_TRK_CONTRAST 214
#define IDC_LBL_CONTRAST 215
#define IDC_CHK_SATURATION 216
#define IDC_TRK_SATURATION 217
#define IDC_LBL_SATURATION 218
// M12 R/G/B gain rows: slider-only (no scroll checkbox by design - the
// scroll gesture must never shift color balance). Same trk/lbl pattern.
#define IDC_TRK_RED 225
#define IDC_LBL_RED 226
#define IDC_TRK_GREEN 228
#define IDC_LBL_GREEN 229
#define IDC_TRK_BLUE 231
#define IDC_LBL_BLUE 232
// M13 GPU vibrance row: slider-only, driven by the NVIDIA driver (NOT DDC),
// deliberately outside AdjustmentCoordinator (docs/03 section 4 exception).
#define IDC_TRK_VIBRANCE 233
#define IDC_LBL_VIBRANCE 234
// M14 volume row: slider-only (no scroll checkbox - human decision, same
// rationale as the R/G/B rows). Monitor speaker volume via VCP 0x62.
#define IDC_TRK_VOLUME 236
#define IDC_LBL_VOLUME 237
#define IDC_CMB_SCOPE 220
#define IDC_CMB_STEP 221
#define IDC_CHK_AUTOSTART 222
#define IDC_BTN_CLOSE 223
