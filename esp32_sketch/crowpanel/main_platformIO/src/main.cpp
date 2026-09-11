#include "pins_config.h"
#include "LovyanGFX_Driver.h"

#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <stdbool.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <vector>
#include <map>

#include "ui.h"

// ==========================================
// ハードウェアピン設定
// ==========================================
#define SD_MOSI 6
#define SD_MISO 4
#define SD_SCK  5
#define SD_CS   7

// ==========================================
// ハードウェア・ディスプレイ用オブジェクト
// ==========================================
LGFX gfx;
RTC_PCF8563 rtc;
unsigned long lastTimeUpdate = 0;

// RTCから起動時に1回だけ取得した基準時刻 (GT911初期化後はRTCへの再アクセスを避けるため)
DateTime bootDateTime;
unsigned long bootMillis = 0;

DateTime getRtcNowCached() {
    unsigned long elapsedSec = (millis() - bootMillis) / 1000;
    return bootDateTime + TimeSpan(elapsedSec);
}

SPIClass SD_SPI = SPIClass(HSPI);

HardwareSerial C6Serial(1);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;
static lv_color_t *buf1;

uint16_t touch_x, touch_y;

LV_FONT_DECLARE(font_jp_14);

unsigned long lastC6DataMillis = 0;
bool c6Connected = false;
unsigned long lastC6StatusCheck = 0;

// MC自動判定用の下限タイム(秒)。0以下の場合は自動判定を行わない
float mcThresholdSeconds = 0.0f;

lv_obj_t * current_editing_row = NULL;

extern int pt_count;
extern bool is_mc;

void on_master_row_clicked(lv_event_t * e) {
    current_editing_row = lv_event_get_target(e);
    lv_obj_clear_flag(ui_Panel4, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * no_label = ui_comp_get_child(current_editing_row, UI_COMP_MASTERROW_MASTERROW1NO);
    lv_obj_t * class_label = ui_comp_get_child(current_editing_row, UI_COMP_MASTERROW_MASTERROW1CLASS);
    lv_textarea_set_placeholder_text(ui_TextArea2, lv_label_get_text(no_label));
    lv_textarea_set_placeholder_text(ui_TextArea3, lv_label_get_text(class_label));

    lv_textarea_set_text(ui_TextArea2, "");
    lv_textarea_set_text(ui_TextArea3, "");

    pt_count = 0;
    is_mc = false;
    lv_label_set_text(ui_Label33, "+ 0 Sec");
    lv_keyboard_set_textarea(ui_Keyboard2, ui_TextArea2);
}

struct RiderInfo {
    String tagId;
    String bib;
    String lastName;
    String firstName;
    String riderClass;
};
std::vector<RiderInfo> riderDatabase;

struct ResultRecord {
    String tagId;
    String bib;
    String name;
    String riderClass;
    float  baseTime;
    int    penaltySeconds;
    bool   isMC;
    float  finalTime;
    String reactText;
    unsigned long recvMillis;
    lv_obj_t * rowObj;
};
std::vector<ResultRecord> masterLogs;

std::map<String, String> pendingNotes;
std::map<String, int> bestIndexByBib;
std::map<String, int> bibBestIndex;   // bib単位でのベスト記録（pb_日付.csv保存専用。ランキング集約とは独立）

std::vector<String> classList;
String masterTargetClass = "X";
String resultFilterClass = "ALL";

int unknownCounter = 0;

void rebuildResultTab();
void registerClass(String className);
void updateClassDropdown();
void savePersonalBestToSD();

void set_backlight(uint8_t brightness) {
    if (brightness > 245) brightness = 245;
    Wire.beginTransmission(0x30);
    Wire.write(brightness);
    Wire.endTransmission();
}
void buzzer_start() { Wire.beginTransmission(0x30); Wire.write(246); Wire.endTransmission(); }
void buzzer_stop()  { Wire.beginTransmission(0x30); Wire.write(247); Wire.endTransmission(); }
void speaker_on()   { Wire.beginTransmission(0x30); Wire.write(248); Wire.endTransmission(); }
void speaker_off()  { Wire.beginTransmission(0x30); Wire.write(249); Wire.endTransmission(); }

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (gfx.getStartCount() > 0) gfx.endWrite();
    gfx.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1, (lgfx::rgb565_t *)&color_p->full);
    lv_disp_flush_ready(disp);
}
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    data->state = LV_INDEV_STATE_REL;
    bool touched = gfx.getTouch(&touch_x, &touch_y);
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_x;
        data->point.y = touch_y;
    }
}

String formatTime(float t) {
    if (t < 0) return "-";
    int minutes = (int)t / 60;
    float seconds = t - (minutes * 60.0f);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%06.3f", minutes, seconds);
    return String(buf);
}

String buildPenMemoText(const ResultRecord &r) {
    if (r.isMC) return "MC";
    if (r.penaltySeconds > 0) return "+" + String(r.penaltySeconds) + "s";
    return "";
}

String buildFullName(const String &lastName, const String &firstName) {
    String trimmedFirst = firstName;
    trimmedFirst.trim();
    if (trimmedFirst.length() == 0) {
        return lastName;
    }
    return lastName + "　" + trimmedFirst;
}

void loadEntryList() {
    riderDatabase.clear();
    File file = SD.open("/entry_list.csv", FILE_READ);
    if (!file) {
        Serial.println("entry_list.csv が見つかりません");
        return;
    }

    bool isHeader = true;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (isHeader) { isHeader = false; continue; }

        int idx1 = line.indexOf(',');
        int idx2 = line.indexOf(',', idx1 + 1);
        int idx3 = line.indexOf(',', idx2 + 1);
        int idx4 = line.indexOf(',', idx3 + 1);
        if (idx1 < 0 || idx2 < 0 || idx3 < 0 || idx4 < 0) continue;

        RiderInfo info;
        info.tagId      = line.substring(0, idx1);
        info.bib        = line.substring(idx1 + 1, idx2);
        info.lastName   = line.substring(idx2 + 1, idx3);
        info.firstName  = line.substring(idx3 + 1, idx4);
        info.riderClass = line.substring(idx4 + 1);
        info.tagId.trim(); info.bib.trim(); info.lastName.trim(); info.firstName.trim(); info.riderClass.trim();

        riderDatabase.push_back(info);
        registerClass(info.riderClass);
    }
    file.close();
    Serial.printf("選手名簿読込完了: %d 名\n", riderDatabase.size());
    updateClassDropdown();
}

RiderInfo* findRiderByTagId(const String &tagId) {
    for (auto &r : riderDatabase) if (r.tagId == tagId) return &r;
    return nullptr;
}
RiderInfo* findRiderByBib(const String &bib) {
    for (auto &r : riderDatabase) if (r.bib == bib) return &r;
    return nullptr;
}

void registerClass(String className) {
    className.trim();
    if (className.length() == 0) return;
    for (auto &c : classList) {
        if (c == className) return;
    }
    classList.push_back(className);
    updateClassDropdown();
}

void updateClassDropdown() {
    String options = "ALL";
    for (auto &c : classList) {
        options += "\n" + c;
    }
    lv_dropdown_set_options(ui_Dropdown1, options.c_str());
}

float getPreviousBestTime(const String &tagId, const String &bib, int excludeIndex) {
    float best = -1.0f;
    for (int i = 0; i < (int)masterLogs.size(); i++) {
        if (i == excludeIndex) continue;
        ResultRecord &r = masterLogs[i];
        if (r.isMC) continue;
        bool sameRider = false;
        if (tagId != "X999") {
            sameRider = (r.tagId == tagId);
        } else {
            sameRider = (r.tagId == "X999"); // X999同士は常に同一人物とみなす
        }
        if (sameRider) {
            if (best < 0 || r.finalTime < best) best = r.finalTime;
        }
    }
    return best;
}

float getOverallTopTime() {
    float top = -1.0f;
    for (auto &r : masterLogs) {
        if (r.isMC) continue;
        if (top < 0 || r.finalTime < top) top = r.finalTime;
    }
    return top;
}

float getClassTopTime(const String &riderClass) {
    float top = -1.0f;
    for (auto &r : masterLogs) {
        if (r.isMC) continue;
        if (r.riderClass != riderClass) continue;
        if (top < 0 || r.finalTime < top) top = r.finalTime;
    }
    return top;
}

void addMasterRowUI(int index) {
    ResultRecord &r = masterLogs[index];

    lv_obj_t * row = ui_MasterRow_create(ui_ListContainer);
    lv_obj_set_user_data(row, (void*)(intptr_t)index);
    lv_obj_add_event_cb(row, on_master_row_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_move_to_index(row, 0);

    lv_obj_t * rank_label  = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1RANK);
    lv_obj_t * class_label = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1CLASS);
    lv_obj_t * no_label    = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1NO);
    lv_obj_t * name_label  = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1NAME);
    lv_obj_t * time_label  = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1TIME);
    lv_obj_t * react_label = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1REACT);
    lv_obj_t * memo_label  = ui_comp_get_child(row, UI_COMP_MASTERROW_MASTERROW1PENMEMO);

    lv_obj_set_style_text_font(name_label, &font_jp_14, 0);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_height(name_label, LV_SIZE_CONTENT);

    lv_label_set_text_fmt(rank_label, "%d", (int)masterLogs.size());
    lv_label_set_text(class_label, r.riderClass.c_str());
    lv_label_set_text(no_label, r.bib.c_str());
    lv_label_set_text(name_label, r.name.c_str());
    lv_label_set_text(time_label, r.isMC ? "MC" : formatTime(r.finalTime).c_str());
    lv_label_set_text(react_label, r.reactText.c_str());
    lv_label_set_text(memo_label, buildPenMemoText(r).c_str());

    r.rowObj = row;
}

void refreshMasterRowUI(int index) {
    ResultRecord &r = masterLogs[index];
    if (r.rowObj == nullptr) return;

    lv_obj_t * class_label = ui_comp_get_child(r.rowObj, UI_COMP_MASTERROW_MASTERROW1CLASS);
    lv_obj_t * no_label    = ui_comp_get_child(r.rowObj, UI_COMP_MASTERROW_MASTERROW1NO);
    lv_obj_t * name_label  = ui_comp_get_child(r.rowObj, UI_COMP_MASTERROW_MASTERROW1NAME);
    lv_obj_t * time_label  = ui_comp_get_child(r.rowObj, UI_COMP_MASTERROW_MASTERROW1TIME);
    lv_obj_t * memo_label  = ui_comp_get_child(r.rowObj, UI_COMP_MASTERROW_MASTERROW1PENMEMO);

    lv_obj_set_style_text_font(name_label, &font_jp_14, 0);
    lv_label_set_text(class_label, r.riderClass.c_str());
    lv_label_set_text(no_label, r.bib.c_str());
    lv_label_set_text(name_label, r.name.c_str());
    lv_label_set_text(time_label, r.isMC ? "MC" : formatTime(r.finalTime).c_str());
    lv_label_set_text(memo_label, buildPenMemoText(r).c_str());
}

void rebuildResultTab() {
    bestIndexByBib.clear();

    for (int i = 0; i < (int)masterLogs.size(); i++) {
        ResultRecord &r = masterLogs[i];
        if (r.isMC) continue;

        // bib単位で集計する（以前はtagId=="X999"の場合だけ全員まとめていたが、
        // 電源再投入後に復元された記録はtagIdが空文字になるため挙動が不整合になっていた。
        // bibは常にユニーク(X1, X2, X3...)なので、bib基準に統一する）
        String bestKey = r.bib;

        auto it = bestIndexByBib.find(bestKey);
        if (it == bestIndexByBib.end()) {
            bestIndexByBib[bestKey] = i;
        } else {
            if (r.finalTime < masterLogs[it->second].finalTime) {
                it->second = i;
            }
        }
    }

    float overallBestTime = -1.0f;
    for (auto &kv : bestIndexByBib) {
        ResultRecord &r = masterLogs[kv.second];
        if (overallBestTime < 0 || r.finalTime < overallBestTime) {
            overallBestTime = r.finalTime;
        }
    }

    std::vector<int> candidates;
    for (auto &kv : bestIndexByBib) {
        ResultRecord &r = masterLogs[kv.second];
        if (resultFilterClass == "ALL" || r.riderClass == resultFilterClass) {
            candidates.push_back(kv.second);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](int a, int b) {
        ResultRecord &ra = masterLogs[a];
        ResultRecord &rb = masterLogs[b];
        if (ra.finalTime != rb.finalTime) return ra.finalTime < rb.finalTime;
        return ra.bib.toInt() < rb.bib.toInt();
    });

    if (candidates.size() > 5) candidates.resize(5);

    float topTime = overallBestTime;

    lv_obj_clean(ui_ListContainer2);

    for (int rank = 0; rank < (int)candidates.size(); rank++) {
        ResultRecord &r = masterLogs[candidates[rank]];

        lv_obj_t * row = ui_ResultRow_create(ui_ListContainer2);

        lv_obj_t * rank_label  = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1RANK);
        lv_obj_t * class_label = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1CLASS);
        lv_obj_t * no_label    = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1NO);
        lv_obj_t * name_label  = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1NAME);
        lv_obj_t * time_label  = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1TIME);
        lv_obj_t * top_label   = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1TOP);
        lv_obj_t * react_label = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1REACT);
        lv_obj_t * memo_label  = ui_comp_get_child(row, UI_COMP_RESULTROW_RESULTROW1PENMEMO);

        lv_obj_set_style_text_font(name_label, &font_jp_14, 0);
        lv_label_set_text_fmt(rank_label, "%d", rank + 1);
        lv_label_set_text(class_label, r.riderClass.c_str());
        lv_label_set_text(no_label, r.bib.c_str());
        lv_label_set_text(name_label, r.name.c_str());
        lv_label_set_text(time_label, formatTime(r.finalTime).c_str());
        lv_label_set_text(react_label, r.reactText.c_str());
        lv_label_set_text(memo_label, buildPenMemoText(r).c_str());

        if (topTime <= 0) {
            lv_label_set_text(top_label, "100.000%");
        } else {
            float ratio = (r.finalTime / topTime) * 100.0f;
            int ratioX1000 = (int)(ratio * 1000.0f);   // 3桁精度で切り捨て
            char buf[16];
            snprintf(buf, sizeof(buf), "%d.%03d%%", ratioX1000 / 1000, ratioX1000 % 1000);
            lv_label_set_text(top_label, buf);
        }
    }
}

// ==========================================
// 個人ベストの永続化 (RTC安全読み取り版)
// ==========================================
String getPersonalBestFilename() {
    DateTime now = getRtcNowCached();
    char filename[32];
    sprintf(filename, "/pb_%04d%02d%02d.csv", now.year(), now.month(), now.day());
    return String(filename);
}

void savePersonalBestToSD() {
    String filename = getPersonalBestFilename();
    File f = SD.open(filename, FILE_WRITE);
    if (!f) {
        Serial.println(filename + " 書き込み失敗");
        return;
    }
    f.println("bib,name,class,time");
    for (auto &kv : bibBestIndex) {   // ★ bib単位の管理に変更（身元不明選手が1枠に集約されないように）
        ResultRecord &best = masterLogs[kv.second];
        f.printf("%s,%s,%s,%.3f\n",
            best.bib.c_str(), best.name.c_str(), best.riderClass.c_str(), best.finalTime);
    }
    f.close();
}

void loadPersonalBestFromSD() {
    String filename = getPersonalBestFilename();
    File f = SD.open(filename, FILE_READ);
    if (!f) {
        Serial.println(filename + " が見つかりません(本日はじめてのセッション)");
        return;
    }

    bool isHeader = true;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (isHeader) { isHeader = false; continue; }

        int idx1 = line.indexOf(',');
        int idx2 = line.indexOf(',', idx1 + 1);
        int idx3 = line.indexOf(',', idx2 + 1);
        if (idx1 < 0 || idx2 < 0 || idx3 < 0) continue;

        ResultRecord rec;
        rec.bib = line.substring(0, idx1);
        rec.name = line.substring(idx1 + 1, idx2);
        rec.riderClass = line.substring(idx2 + 1, idx3);
        rec.finalTime = line.substring(idx3 + 1).toFloat();
        rec.baseTime = rec.finalTime;
        rec.penaltySeconds = 0;
        rec.isMC = false;
        rec.tagId = "";
        rec.rowObj = nullptr;

        masterLogs.push_back(rec);
        int idx = masterLogs.size() - 1;
        bestIndexByBib[rec.bib] = idx;
        bibBestIndex[rec.bib] = idx;   // ★ bib単位の管理にも復元する

        // bibが"X"+数字(例:"X12")の形式なら、unknownCounterを復元して番号の衝突を防ぐ
        if (rec.bib.startsWith("X")) {
            int num = rec.bib.substring(1).toInt();
            if (num > unknownCounter) unknownCounter = num;
        }

        registerClass(rec.riderClass);
    }
    f.close();
    Serial.printf("個人ベスト復元完了(%s): %d 名\n", filename.c_str(), bestIndexByBib.size());
}

// ==========================================
// MC threshold (下限タイムによる自動MC判定)
// ==========================================
void setMcThreshold(int minutes, int seconds, int ms100, int ms10, int ms1) {
    int milliseconds = ms100 * 100 + ms10 * 10 + ms1;
    mcThresholdSeconds = minutes * 60.0f + seconds + milliseconds / 1000.0f;
    Serial.printf("[MC閾値] 設定: %d分%02d秒.%03d (%.3f秒)\n",
        minutes, seconds, milliseconds, mcThresholdSeconds);
}

void processResult(String tagId, float rawTime) {
    RiderInfo* rider = findRiderByTagId(tagId);
    String bib, name, riderClass;

    if (tagId == "X999") {
        unknownCounter++;
        bib = "X" + String(unknownCounter);   // 例: "X1", "X15" など、短く表記
        name = "";
        riderClass = masterTargetClass;
    } else if (rider == nullptr) {
        bib = tagId;
        name = "";
        riderClass = masterTargetClass;
    } else {
        bib = rider->bib;
        name = buildFullName(rider->lastName, rider->firstName);
        riderClass = rider->riderClass;
    }

    registerClass(riderClass);

    ResultRecord rec;
    rec.tagId = tagId;
    rec.bib = bib;
    rec.name = name;
    rec.riderClass = riderClass;
    rec.baseTime = rawTime;
    rec.penaltySeconds = 0;

    // 下限タイムを下回っていたら、自動的にMC判定
    if (mcThresholdSeconds > 0 && rawTime < mcThresholdSeconds) {
        rec.isMC = true;
        Serial.printf("[自動MC] タイム %.3f秒 < 閾値 %.3f秒 のため、MC判定\n", rawTime, mcThresholdSeconds);
    } else {
        rec.isMC = false;
    }

    rec.finalTime = rec.isMC ? INFINITY : rawTime;
    rec.recvMillis = millis();
    rec.rowObj = nullptr;

    auto noteIt = pendingNotes.find(tagId);
    if (noteIt != pendingNotes.end()) {
        rec.reactText = noteIt->second;
        pendingNotes.erase(noteIt);
    }

    masterLogs.push_back(rec);
    int newIndex = masterLogs.size() - 1;

    // bib単位でのベスト記録を更新（保存用。ランキング集約(bestIndexByBib)とは独立して管理する）
    {
        auto bIt = bibBestIndex.find(bib);
        if (bIt == bibBestIndex.end() || masterLogs[newIndex].finalTime < masterLogs[bIt->second].finalTime) {
            bibBestIndex[bib] = newIndex;
        }
    }

    addMasterRowUI(newIndex);

    long total_ms = (long)round(rawTime * 1000.0);
    Serial0.printf("T:%ld\n", total_ms);

    // MC判定された場合、ファステスト系の読み上げは行わない(除外ロジックにより自動的に対象外)
    if (!rec.isMC) {
        float prevBest = getPreviousBestTime(tagId, bib, newIndex);
        bool isPersonalBest = (prevBest < 0 || rawTime < prevBest);

        float overallTop = getOverallTopTime();
        bool isOverallFastest = (fabs(rawTime - overallTop) < 0.0005f);

        if (isOverallFastest) {
            Serial0.println("W:overall_fastest");
        } else {
            float classTop = getClassTopTime(riderClass);
            bool isClassFastest = (fabs(rawTime - classTop) < 0.0005f);

            if (isClassFastest) {
                Serial0.println("W:class_fastest");
            }
            if (isPersonalBest) {
                Serial0.println("W:personal_best");
            }

            if (overallTop > 0) {
                float ratio = (rawTime / overallTop) * 100.0f;
                int ratioX10 = (int)(ratio * 10.0f); // 直接1桁精度で切り捨て
                Serial0.printf("P:%d\n", ratioX10);
            }
        }
    }

    rebuildResultTab();
    savePersonalBestToSD();
}

void processJsonMessage(String jsonStr) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, jsonStr)) return;

    String type = doc["type"].as<String>();
    String id = doc["id"] | "X999";

    if (type == "RESULT") {
        float t = doc["time"].as<float>();
        processResult(id, t);

    } else if (type == "REACTION") {
        float diff = doc["diff"] | 0.0f;
        pendingNotes[id] = "React:" + String(diff, 3) + "s";

    } else if (type == "FLYING") {
        float diff = doc["diff"] | 0.0f;
        pendingNotes[id] = "FLYING(" + String(diff, 3) + "s)";

    } else if (type == "SEQ_START") {
        Serial.println("[SEQ] シグナル開始");

    } else if (type == "FORCE_DNF") {
        Serial.println("[SEQ] コースリセット");
    }
}

void importCsvData() {
    Serial.println("UIから importCsvData が呼ばれました！");
    loadEntryList();
}

void sendManualTrigger(String type) {
    C6Serial.println(type);
    Serial.println("[MANUAL] " + type + " を送信しました");
}

void queueResultVoice(float total_seconds) {
    long ms = (long)round(total_seconds * 1000.0);
    Serial0.printf("T:%ld\n", ms);
}

// ==========================================
// RTC時刻設定 (予約ファイル + 再起動方式)
// setup()の、早い段階(GT911初期化前)なら、確実に書き込めることが
// 確認できたため、その場で書き込むのではなく、
// 「次回起動時に書き込む」予約をSDに残し、即座に再起動する
// ==========================================
void setRtcTime(int year, int month, int day, int hour, int minute) {
    File f = SD.open("/pending_rtc.txt", FILE_WRITE);
    if (f) {
        f.printf("%d,%d,%d,%d,%d\n", year, month, day, hour, minute);
        f.close();
        Serial.printf("[RTC] 予約ファイル保存: %04d/%02d/%02d %02d:%02d → 再起動します\n",
            year, month, day, hour, minute);
    } else {
        Serial.println("[RTC] 予約ファイルの書き込みに失敗しました");
    }
    delay(300);
    ESP.restart();
}

void exportMasterListToCSV() {
    DateTime now = getRtcNowCached();
    char baseFilename[32];
    sprintf(baseFilename, "/%04d%02d%02d_master", now.year(), now.month(), now.day());

    int suffix = 1;
    String filename = String(baseFilename) + "_" + String(suffix) + ".csv";
    while (SD.exists(filename)) {
        suffix++;
        filename = String(baseFilename) + "_" + String(suffix) + ".csv";
    }

    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("エラー：SDカードにファイルを作成できませんでした！");
        return;
    }

    file.println("Order,Class,No,Name,BaseTime,Penalty,IsMC,FinalTime,React");
    for (auto &r : masterLogs) {
        file.printf("%s,%s,%s,%.3f,%d,%d,%.3f,%s\n",
            r.riderClass.c_str(), r.bib.c_str(), r.name.c_str(),
            r.baseTime, r.penaltySeconds, r.isMC ? 1 : 0, r.finalTime, r.reactText.c_str());
    }

    file.close();
    Serial.println("Export完了！(" + filename + ")");
}

void commitEditedResult(String new_no, String new_class, int pylon_touch, bool is_miss_course) {
    if (current_editing_row == NULL) return;

    int index = (int)(intptr_t)lv_obj_get_user_data(current_editing_row);
    if (index < 0 || index >= (int)masterLogs.size()) return;

    ResultRecord &r = masterLogs[index];

    if (new_no.length() > 0 && new_no != r.bib) {
        r.bib = new_no;
        RiderInfo* rider = findRiderByBib(new_no);
        if (rider != nullptr) {
            r.name = buildFullName(rider->lastName, rider->firstName);
            r.riderClass = rider->riderClass;
        }
    }
    if (new_class.length() > 0 && new_class != r.riderClass) {
        r.riderClass = new_class;
        registerClass(new_class);
    }

    r.penaltySeconds = pylon_touch;
    r.isMC = is_miss_course;
    r.finalTime = r.isMC ? INFINITY : (r.baseTime + (float)r.penaltySeconds);

    // bib単位でのベスト記録を更新（bib自体が編集で変わった場合にも対応）
    {
        auto bIt = bibBestIndex.find(r.bib);
        if (bIt == bibBestIndex.end() || r.finalTime < masterLogs[bIt->second].finalTime) {
            bibBestIndex[r.bib] = index;
        }
    }

    refreshMasterRowUI(index);
    rebuildResultTab();
    savePersonalBestToSD();
}

void onResultFilterChanged() {
    char buf[32];
    lv_dropdown_get_selected_str(ui_Dropdown1, buf, sizeof(buf));
    resultFilterClass = String(buf);
    rebuildResultTab();
}

void onMasterTargetClassSet() {
    const char* text = lv_textarea_get_text(ui_TextArea1);
    String newClass = String(text);
    newClass.trim();
    if (newClass.length() > 0) {
        masterTargetClass = newClass;
        registerClass(newClass);
        Serial.println("[Master] Target Class設定: " + masterTargetClass);
    }
}

// ==========================================
// 時刻表示更新 (RTC安全読み取り版)
// ==========================================
void updateTimeDisplay() {
    DateTime now = getRtcNowCached();
    char timeStr[32];
    sprintf(timeStr, "%04d/%02d/%02d %02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute());
    if (ui_uiLabelCurrentTime != NULL) {
        lv_label_set_text(ui_uiLabelCurrentTime, timeStr);
    }
}

void updateC6ConnectionStatus() {
    bool nowConnected = (millis() - lastC6DataMillis < 15000);
    if (nowConnected != c6Connected) {
        c6Connected = nowConnected;
        if (ui_Label14 != NULL) {
            lv_label_set_text(ui_Label14, c6Connected ? "session: OK" : "session: NG");
            lv_obj_set_style_text_color(ui_Label14,
                c6Connected ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000), 0);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    if (psramFound()) {
        Serial.printf("[BOOT] PSRAM検出: %d バイト\n", ESP.getPsramSize());
    } else {
        Serial.println("[BOOT] PSRAM未検出！");
    }

    C6Serial.begin(115200, SERIAL_8N1, /*RX*/ 2, /*TX*/ 8);
    Serial0.begin(115200, SERIAL_8N1, /*RX*/ 44, /*TX*/ 43);

    Wire.begin(15, 16);
    Wire.setClock(50000);
    delay(50);

    Wire.beginTransmission(0x30);
    Wire.write(250);
    Wire.endTransmission();
    delay(100);

    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
    delay(20);
    digitalWrite(1, HIGH);
    delay(100);
    pinMode(1, INPUT);
    delay(200);

    set_backlight(0);

    if (!rtc.begin(&Wire)) {
        Serial.println("RTC Not Found");
    } else {
        Serial.println("RTC Connected");
        bootDateTime = rtc.now(); // ここで1回だけ確実に読む(GT911初期化前)
        bootMillis = millis();
        Serial.printf("[BOOT] 起動時刻キャッシュ: %04d/%02d/%02d %02d:%02d:%02d\n",
            bootDateTime.year(), bootDateTime.month(), bootDateTime.day(),
            bootDateTime.hour(), bootDateTime.minute(), bootDateTime.second());
    }

    SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SD_SPI, 80000000)) {
        Serial.println("エラー：SDカードが認識できません！(HSPI)");
    } else {
        Serial.println("SDカードマウント成功！");

        // ★予約されたRTC設定があれば、この、まだ安全なタイミングで、反映する
        if (SD.exists("/pending_rtc.txt")) {
            File f = SD.open("/pending_rtc.txt", FILE_READ);
            if (f) {
                String line = f.readStringUntil('\n');
                f.close();
                int y, mo, d, h, mi;
                if (sscanf(line.c_str(), "%d,%d,%d,%d,%d", &y, &mo, &d, &h, &mi) == 5) {
                    DateTime newTime(y, mo, d, h, mi, 0);
                    rtc.adjust(newTime);
                    delay(100);
                    bootDateTime = rtc.now();
                    bootMillis = millis();
                    Serial.printf("[RTC] 予約設定を反映しました: %04d/%02d/%02d %02d:%02d\n", y, mo, d, h, mi);
                } else {
                    Serial.println("[RTC] 予約ファイルの内容が不正でした");
                }
            }
            SD.remove("/pending_rtc.txt");
        }
    }

    gfx.init();
    gfx.initDMA();
    gfx.startWrite();
    gfx.fillScreen(TFT_BLACK);

    lv_init();
    size_t buffer_size = sizeof(lv_color_t) * LCD_H_RES * LCD_V_RES;
    buf = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    buf1 = (lv_color_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&draw_buf, buf, buf1, LCD_H_RES * LCD_V_RES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    delay(100);
    gfx.fillScreen(TFT_BLACK);

    ui_init();

    lv_obj_add_event_cb(ui_TextArea2, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui_TextArea3, on_ta_focused, LV_EVENT_FOCUSED, NULL);

    updateClassDropdown();

    if (SD.cardType() != CARD_NONE) {
        loadEntryList();
        loadPersonalBestFromSD();
        rebuildResultTab();
    }

    Serial.println("Setup done. UI is running.");
}

void loop() {
    lv_timer_handler();
    delay(5);

    if (millis() - lastTimeUpdate > 1000) {
        updateTimeDisplay();
        lastTimeUpdate = millis();
    }

    if (millis() - lastC6StatusCheck > 10000) {
        updateC6ConnectionStatus();
        lastC6StatusCheck = millis();
    }

    if (C6Serial.available() > 0) {
        String input = C6Serial.readStringUntil('\n');
        input.trim();

        lastC6DataMillis = millis();

        Serial.println(input);

        if (input.startsWith("[ESP_DATA]")) {
            input.replace("[ESP_DATA]", "");
            input.trim();
            processJsonMessage(input);
        }
    }

    if (Serial.available() > 0) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.startsWith("[ESP_DATA]")) {
            input.replace("[ESP_DATA]", "");
            input.trim();
            processJsonMessage(input);
        } else if (input.startsWith("{")) {
            processJsonMessage(input);
        }
    }
}