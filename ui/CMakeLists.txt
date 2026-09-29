SET(SOURCES ui_Screen1.c
    ui_Screen2.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_water_png.c
    ui_img_a2_png.c
    ui_img_baseline_bluetooth_connected_black_24dp_png.c
    ui_img_baseline_wifi_black_24dp_png.c
    ui_img_wuko_png.c
    fonts/ui_font_ClockFont.c
    fonts/ui_font_Font1.c
    fonts/ui_font_Font2.c)

add_library(ui ${SOURCES})
