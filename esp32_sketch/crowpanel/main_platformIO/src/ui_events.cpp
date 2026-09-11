#include "ui.h"
#include <Arduino.h>
#include <stdlib.h> // atoi関数用

// ====================================================================
// main.cpp 側に実装されている関数の呼び出し宣言（ブリッジ）
// ====================================================================
extern void sendManualTrigger(String type);
extern void importCsvData();
extern void queueResultVoice(float total_seconds);
extern void commitEditedResult(String new_no, String new_class, int pylon_touch, bool is_miss_course);
extern void exportMasterListToCSV();
extern void setRtcTime(int year, int month, int day, int hour, int minute);
extern void onResultFilterChanged();
extern void onMasterTargetClassSet();
extern void setMcThreshold(int minutes, int seconds, int ms100, int ms10, int ms1);

// ====================================================================
// 状態管理用の変数
// ====================================================================
float base_time = 0.0;   // 受信した素のタイム（秒）
int pt_count = 0;        // PTの回数（1回 = +1秒）
bool is_mc = false;      // ミスコース状態

// ====================================================================
// 内部関数（C++専用）
// ====================================================================
void update_result_display() {
    if (is_mc) {
        lv_label_set_text(ui_Label30, "MC");
    } else {
        float final_time = base_time + (float)pt_count;
        int minutes = (int)final_time / 60;
        float seconds = final_time - (minutes * 60.0);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%06.3f", minutes, seconds);
        lv_label_set_text(ui_Label30, buf);
    }
}

void set_received_result(float raw_time) {
    base_time = raw_time;
    pt_count = 0;
    is_mc = false;
    update_result_display();
}

// ====================================================================
// ここから下は SquareLine(C言語) から呼ばれるため extern "C" で囲む
// ====================================================================
extern "C" {

// --- Manage (リザルト・ペナルティ管理) 関連 ---
void on_class_filter_changed(lv_event_t * e) {
    Serial.println("[UI] Class Filter Changed");
    onResultFilterChanged();
}

void on_ta_focused(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target(e);
    lv_obj_clear_flag(ui_Keyboard2, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(ui_Keyboard2, target);
    lv_keyboard_set_mode(ui_Keyboard2, LV_KEYBOARD_MODE_TEXT_UPPER);
}

void on_set_clicked(lv_event_t * e) {
    lv_obj_add_flag(ui_Keyboard2, LV_OBJ_FLAG_HIDDEN);
    onMasterTargetClassSet();
}

void on_pt_clear_clicked(lv_event_t * e) {
    pt_count = 0;
    if (is_mc) return;
    lv_label_set_text(ui_Label33, "+ 0 Sec");
}

void on_pt_add_clicked(lv_event_t * e) {
    pt_count += 1;
    if (is_mc) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "+ %d Sec", pt_count);
    lv_label_set_text(ui_Label33, buf);
}

void on_mc_clicked(lv_event_t * e) {
    is_mc = !is_mc;
    if (is_mc) {
        lv_label_set_text(ui_Label33, "MC");
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "+ %d Sec", pt_count);
        lv_label_set_text(ui_Label33, buf);
    }
}

// --- 確認ポップアップ / インポート ---
void on_dialog_ok(lv_event_t * e) {
    lv_obj_add_flag(ui_Keyboard2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Panel4, LV_OBJ_FLAG_HIDDEN);

    String input_no = lv_textarea_get_text(ui_TextArea2);
    String input_class = lv_textarea_get_text(ui_TextArea3);
    commitEditedResult(input_no, input_class, pt_count, is_mc);
}

void on_dialog_cancel(lv_event_t * e) {
    lv_obj_add_flag(ui_Keyboard2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Panel4, LV_OBJ_FLAG_HIDDEN);
}

void on_import_clicked(lv_event_t * e) {
    importCsvData();
}

void on_start_clicked(lv_event_t * e) {
    sendManualTrigger("START");
}

void on_goal_clicked(lv_event_t * e) {
    sendManualTrigger("FORCE_DNF");
}

// --- 時刻設定・エクスポート関連 ---
void on_set_time_clicked(lv_event_t * e) {
    lv_obj_clear_flag(ui_Panel5, LV_OBJ_FLAG_HIDDEN);
}

void on_time_dialog_ok(lv_event_t * e) {
    char buf[16];
    lv_roller_get_selected_str(ui_Roller1, buf, sizeof(buf)); int year = atoi(buf);
    lv_roller_get_selected_str(ui_Roller2, buf, sizeof(buf)); int month = atoi(buf);
    lv_roller_get_selected_str(ui_Roller3, buf, sizeof(buf)); int day = atoi(buf);
    lv_roller_get_selected_str(ui_Roller4, buf, sizeof(buf)); int hour = atoi(buf);
    lv_roller_get_selected_str(ui_Roller5, buf, sizeof(buf)); int minute = atoi(buf);
    // ★デバッグログ(念のため、原因が完全に解消されたか確認するため)
    Serial.printf("[DEBUG] 設定値: year=%d, month=%d, day=%d, hour=%d, minute=%d\n",
        year, month, day, hour, minute);
    // main.cppの関数を呼んでRTCに書き込む
    setRtcTime(year, month, day, hour, minute);

    lv_obj_add_flag(ui_Panel5, LV_OBJ_FLAG_HIDDEN);
}

void on_time_dialog_cancel(lv_event_t * e) {
    lv_obj_add_flag(ui_Panel5, LV_OBJ_FLAG_HIDDEN);
}

void on_export_clicked(lv_event_t * e) {
    exportMasterListToCSV();
}

// --- MC threshold(下限タイムによる自動MC判定)関連 ---
void on_set_mc_threshold_clicked(lv_event_t * e) {
    lv_obj_clear_flag(ui_Panel2, LV_OBJ_FLAG_HIDDEN);
}

void on_mc_threshold_dialog_ok(lv_event_t * e) {
    int minutes = lv_roller_get_selected(ui_Roller6);
    int seconds = lv_roller_get_selected(ui_Roller7);
    int ms100    = lv_roller_get_selected(ui_Roller8);
    int ms10     = lv_roller_get_selected(ui_Roller9);
    int ms1      = lv_roller_get_selected(ui_Roller10);

    setMcThreshold(minutes, seconds, ms100, ms10, ms1);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d.%d%d%d", minutes, seconds, ms100, ms10, ms1);
    lv_label_set_text(ui_uiLabelMCTime, buf);

    lv_obj_add_flag(ui_Panel2, LV_OBJ_FLAG_HIDDEN);
}

void on_mc_threshold_dialog_cancel(lv_event_t * e) {
    lv_obj_add_flag(ui_Panel2, LV_OBJ_FLAG_HIDDEN);
}

} // extern "C" 終わり