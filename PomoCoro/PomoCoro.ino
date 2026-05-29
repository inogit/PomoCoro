/*
 * PomoCoro  -  ATOMS3で作る傾けて操作するポモドーロタイマー
 * ============================================================
 * 必要ライブラリ (Arduino IDE のライブラリマネージャーからインストール)
 *   - M5Unified   (ボードマネージャー: M5Stack by M5Stack)
 *   - ArduinoJson (Benoit Blanchon 著)
 *
 * 向き操作 (ATOMS3を机に垂直に立てて使用):
 *   垂直・正面    → アイドル       : 時計 + 天気
 *   右に90°転がす → ポモドーロ     : 25分集中 → 5分休憩 (繰り返し)
 *   さらに180°    → 長い休憩       : 15分 or 20分タイマー
 *   さらに180°    → 今日の統計     : 完了セッション数・累計集中時間
 *   水平に置く    → 設定モード     : ボタンで 15分↔20分 切替
 *   左に転がす    → 逆方向に遷移
 *
 * 注意: 初回は先に calibration/calibration.ino を実行して
 *       IMU軸の符号を確認してください (下記 "IMUキャリブレーション" セクション)
 * ============================================================
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// ============================================================
//  ★ ユーザー設定エリア  ここを書き換えてください
// ============================================================

// --- WiFi設定 ---
// プライマリ (自宅など、普段使いの環境)
const char* WIFI_SSID_1 = "YOUR_SSID_1";
const char* WIFI_PASS_1 = "YOUR_PASSWORD_1";

// セカンダリ (外出時のポケットWiFiなど)
// 使わない場合は "" のままにしてください
const char* WIFI_SSID_2 = "YOUR_SSID_2";
const char* WIFI_PASS_2 = "YOUR_PASSWORD_2";

// 各WiFiへの接続試行時間 (秒)
const int WIFI_TIMEOUT_SEC = 10;

// --- 位置情報 ---
// Googleマップで場所を右クリックすると緯度・経度を確認できます
// 例: 横浜の場合 LAT=35.46609658769698, LON=139.62205767612298
const float LOC_LAT = 35.46f;
const float LOC_LON = 139.62f;

// ============================================================
//  IMUキャリブレーション
//  calibration.ino を実行して確認した符号をここに反映してください
//
//  ATOMS3を各向きにしたときの目安:
//    垂直(正面・USB下) : 支配軸=Y, 符号= +1 or -1
//    右に90°           : 支配軸=X, 符号= +1 or -1
//    逆さま(USB上)     : 支配軸=Y, 符号= 上記と逆
//    左に90°           : 支配軸=X, 符号= 上記と逆
//    水平置き          : 支配軸=Z, 符号= +1 or -1
//
//  以下はよく見られるデフォルト値。合わない場合は符号を反転してください。
// ============================================================
// 垂直(アイドル)の判定: Yが正なら +1、負なら -1
static const int UPRIGHT_AXIS_SIGN = +1;  // ay > 0 → アイドル方向
// 右に倒した時の判定: Xが正なら +1、負なら -1  
static const int RIGHT_AXIS_SIGN   = -1;  // ax < 0 → 右に倒した方向

// ============================================================
//  定数
// ============================================================
const int WORK_SEC        = 25 * 60;
const int SHORT_BREAK_SEC =  5 * 60;

const unsigned long WEATHER_INTERVAL_MS = 10UL * 60 * 1000; // 10分ごとに更新
const unsigned long ORI_DEBOUNCE_MS     = 700;               // 誤検知防止の待機時間

// 閾値: これ以上の加速度があれば「その軸が支配的」と判断
const float AXIS_THRESH  = 0.65f;  // 通常軸
const float FLAT_THRESH  = 0.80f;  // 水平置き検出 (高めに設定して誤発火防止)

// ============================================================
//  カラーテーマ (RGB888)
// ============================================================
const uint32_t C_IDLE_BG  = 0x0D1B2A;
const uint32_t C_IDLE_FG  = 0xECEFF4;
const uint32_t C_WORK_BG  = 0x6B1A1A;
const uint32_t C_WORK_FG  = 0xFFEEEE;
const uint32_t C_SBRK_BG  = 0x0A2214;  // BREAKドーナツが映えるよう暗く
const uint32_t C_SBRK_FG  = 0xEEFFEE;
const uint32_t C_LBRK_BG  = 0x0D2B4A;
const uint32_t C_LBRK_FG  = 0xEEF4FF;
const uint32_t C_STAT_BG  = 0x1E1040;
const uint32_t C_STAT_FG  = 0xEEEEFF;
const uint32_t C_SETT_BG  = 0x1A1A1A;
const uint32_t C_SETT_FG  = 0xDDDDDD;
const uint32_t C_ACCENT   = 0xFF9000;
const uint32_t C_BAR_WORK = 0xDD5555;  // CIRCLEと同色
const uint32_t C_BAR_BRK  = 0x55EE88;  // BREAK: 明るく視認性向上

// ============================================================
//  列挙型
// ============================================================
enum DeviceState   { ST_IDLE=0, ST_POMODORO, ST_LONG_BREAK, ST_STATS, ST_SETTINGS };
enum PomodoroPhase { PH_WORK,   PH_SHORT_BREAK };
enum Orientation   {
  ORI_UPRIGHT = 0,  // →  ST_IDLE
  ORI_RIGHT,        // →  ST_POMODORO
  ORI_UPSIDE,       // →  ST_LONG_BREAK
  ORI_LEFT,         // →  ST_STATS
  ORI_FLAT,         // →  ST_SETTINGS
  ORI_UNKNOWN
};

// 向き → 状態マッピングテーブル
const DeviceState ORI_TO_STATE[] = {
  ST_IDLE, ST_POMODORO, ST_LONG_BREAK, ST_STATS, ST_SETTINGS
};

// ============================================================
//  グローバル変数
// ============================================================
DeviceState    gState       = ST_IDLE;
PomodoroPhase  gPomPhase    = PH_WORK;

Orientation    gCurOri      = ORI_UNKNOWN;
Orientation    gStableOri   = ORI_UNKNOWN;
unsigned long  gOriSince    = 0;

unsigned long  gTimerStart  = 0;
int            gTimerSec    = 0;
bool           gTimerActive = false;
bool           gAlertShown  = false;

int  gLongBreakMin  = 15;
bool gCircleTimer   = false;  // false=デジタル表示, true=円形表示
int  gTodayPom      = 0;
int  gTodayFocusSec = 0;
int  gLastResetDay  = -1;

float gWeatherTemp  = 0.0f;
int   gWeatherCode  = 0;
bool  gWeatherReady = false;
unsigned long gLastWeather = 0;

bool          gForceRedraw = true;
int           gPrevSecLeft = -1;
unsigned long gLastDraw    = 0;

Preferences prefs;

// ============================================================
//  前方宣言
// ============================================================
void enterState(DeviceState s);

// ============================================================
//  描画ユーティリティ
// ============================================================

// テキストを水平中央揃えで描画 (y = 上端座標)
void drawCenter(const char* text, int y) {
  int tw = M5.Display.textWidth(text);
  int x  = (M5.Display.width() - tw) / 2;
  if (x < 0) x = 0;
  M5.Display.setCursor(x, y);
  M5.Display.print(text);
}

// プログレスバー描画
void drawBar(int remaining, int total, int y, uint32_t color) {
  const int bx = 8, bw = 112, bh = 24;  // シミュレーターと同じ高さ
  const int r  = bh / 2;                 // 角丸半径

  // --- 背景トラック (角丸) ---
  M5.Display.fillRoundRect(bx, y, bw, bh, r, 0x1a2535);

  // --- 残り時間バー (角丸) ---
  if (total > 0 && remaining > 0) {
    int fill = (int)((float)remaining / total * (float)bw);
    fill = constrain(fill, 0, bw);
    M5.Display.fillRoundRect(bx, y, fill, bh, r, color);
  }
}

// ============================================================
//  方向検出
// ============================================================
Orientation detectOrientation() {
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  // デバッグ出力: 以下の行のコメントを外すとシリアルモニタで確認できます
  // Serial.printf("ax=%.2f ay=%.2f az=%.2f\n", ax, ay, az);

  float aax = fabsf(ax), aay = fabsf(ay), aaz = fabsf(az);

  // 水平置き: Z軸が強く支配的
  if (aaz > FLAT_THRESH && aaz > aax && aaz > aay) {
    return ORI_FLAT;
  }

  // Y軸が支配的: 垂直方向
  if (aay >= AXIS_THRESH && aay >= aax && aay >= aaz) {
    bool isUp = (UPRIGHT_AXIS_SIGN > 0) ? (ay > 0.0f) : (ay < 0.0f);
    return isUp ? ORI_UPRIGHT : ORI_UPSIDE;
  }

  // X軸が支配的: 左右に倒れている
  if (aax >= AXIS_THRESH && aax > aay && aax > aaz) {
    bool isRight = (RIGHT_AXIS_SIGN > 0) ? (ax > 0.0f) : (ax < 0.0f);
    return isRight ? ORI_RIGHT : ORI_LEFT;
  }

  return ORI_UNKNOWN;
}

// ============================================================
//  タイマー
// ============================================================
void startTimer(int totalSec) {
  gTimerStart  = millis();
  gTimerSec    = totalSec;
  gTimerActive = true;
  gAlertShown  = false;
}

int timerRemaining() {
  if (!gTimerActive) return 0;
  long elapsed = (long)((millis() - gTimerStart) / 1000UL);
  return max(0, gTimerSec - (int)elapsed);
}

bool timerDone() {
  return gTimerActive && timerRemaining() <= 0;
}

// ============================================================
//  天気アイコン描画
//  cx, cy: アイコン中心座標  sz: 基準サイズ(半径相当)
// ============================================================

// 晴れ: 円 + 放射線
void drawIconSun(int cx, int cy, int sz) {
  uint32_t col = 0xFFCC00;
  M5.Display.fillCircle(cx, cy, sz / 2, col);
  for (int i = 0; i < 8; i++) {
    float a = i * 3.14159f / 4.0f;
    int x1 = cx + (int)((sz / 2 + 2) * cosf(a));
    int y1 = cy + (int)((sz / 2 + 2) * sinf(a));
    int x2 = cx + (int)((sz - 1)     * cosf(a));
    int y2 = cy + (int)((sz - 1)     * sinf(a));
    M5.Display.drawLine(x1, y1, x2, y2, col);
  }
}

// 雲: 複数の円を重ねる
void drawIconCloud(int cx, int cy, int sz, uint32_t col) {
  int r = sz / 3;
  M5.Display.fillCircle(cx - r,     cy,     r,         col);
  M5.Display.fillCircle(cx,         cy - r, r + 1,     col);
  M5.Display.fillCircle(cx + r,     cy,     r,         col);
  M5.Display.fillRect  (cx - r * 2, cy,     r * 4, r,  col);
}

// 曇り: 薄い太陽 + 白雲
void drawIconCloudy(int cx, int cy, int sz) {
  drawIconSun(cx - sz / 5, cy - sz / 5, sz * 3 / 4);  // 小さめ太陽を左上に
  drawIconCloud(cx + sz / 6, cy + sz / 5, sz, 0xCCCCCC);
}

// 雨: 雲 + 雨粒
void drawIconRain(int cx, int cy, int sz) {
  drawIconCloud(cx, cy - sz / 5, sz, 0x8899AA);
  uint32_t rc = 0x44AAFF;
  int drop = sz / 4;
  for (int i = -1; i <= 1; i++) {
    int rx = cx + i * drop;
    int ry = cy + sz / 3;
    M5.Display.fillTriangle(rx, ry + drop, rx - 2, ry, rx + 2, ry, rc);
  }
}

// 雪: 雲 + 雪の結晶（*）
void drawIconSnow(int cx, int cy, int sz) {
  drawIconCloud(cx, cy - sz / 5, sz, 0xAABBCC);
  uint32_t sc = 0xDDEEFF;
  int sy = cy + sz / 3;
  for (int i = -1; i <= 1; i++) {
    int sx2 = cx + i * (sz / 3);
    M5.Display.drawFastVLine(sx2,     sy - sz/5, sz/3, sc);
    M5.Display.drawFastHLine(sx2-sz/10, sy,      sz/5, sc);
  }
}

// 霧: 横線を複数
void drawIconFog(int cx, int cy, int sz) {
  uint32_t fc = 0x99AABB;
  for (int i = -2; i <= 2; i++) {
    int lw = (abs(i) == 2) ? sz * 3 / 4 : sz;
    M5.Display.drawFastHLine(cx - lw/2, cy + i * (sz/4), lw, fc);
  }
}

// 嵐: 雲 + 稲妻
void drawIconStorm(int cx, int cy, int sz) {
  drawIconCloud(cx, cy - sz / 5, sz, 0x556677);
  // 稲妻
  uint32_t lc = 0xFFFF00;
  int bx = cx, by = cy + sz / 5;
  M5.Display.drawLine(bx,       by,          bx - sz/4, by + sz/3, lc);
  M5.Display.drawLine(bx-sz/4,  by + sz/3,   bx,        by + sz/4, lc);
  M5.Display.drawLine(bx,       by + sz/4,   bx - sz/3, by + sz*2/3, lc);
}

// 天気コードからアイコンを描画
void drawWeatherIcon(int code, int cx, int cy, int sz) {
  if (code == 0)        drawIconSun   (cx, cy, sz);
  else if (code <= 3)   drawIconCloudy(cx, cy, sz);
  else if (code <= 48)  drawIconFog   (cx, cy, sz);
  else if (code <= 57)  drawIconRain  (cx, cy, sz);  // 霧雨
  else if (code <= 67)  drawIconRain  (cx, cy, sz);
  else if (code <= 77)  drawIconSnow  (cx, cy, sz);
  else if (code <= 82)  drawIconRain  (cx, cy, sz);  // 驟雨
  else if (code <= 86)  drawIconSnow  (cx, cy, sz);
  else                  drawIconStorm (cx, cy, sz);
}

// ============================================================
//  天気取得 (Open-Meteo API)
// ============================================================
const char* wCodeToText(int c) {
  if (c == 0)  return "CLEAR";
  if (c <= 3)  return "CLOUDY";
  if (c <= 48) return "FOG";
  if (c <= 57) return "DRIZZLE";
  if (c <= 67) return "RAIN";
  if (c <= 77) return "SNOW";
  if (c <= 82) return "SHOWER";
  if (c <= 86) return "SLEET";
  return "STORM";
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[256];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=%.2f&longitude=%.2f"
    "&current=temperature_2m,weather_code"
    "&timezone=Asia%%2FTokyo",
    LOC_LAT, LOC_LON);

  WiFiClientSecure client;
  client.setInsecure();  // SSL証明書検証省略 (ホビー用途)
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(8000);

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String body = http.getString();
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, body)) {
      gWeatherTemp  = doc["current"]["temperature_2m"] | 0.0f;
      gWeatherCode  = doc["current"]["weather_code"]   | 0;
      gWeatherReady = true;
    }
  }
  http.end();
  gLastWeather = millis();
}

// ============================================================
//  タイマー完了アラート (画面フラッシュ)
// ============================================================
void flashAlert(uint32_t color1, uint32_t color2) {
  for (int i = 0; i < 5; i++) {
    M5.Display.fillScreen(color1);
    delay(180);
    M5.Display.fillScreen(color2);
    delay(100);
  }
}

// ============================================================
//  状態入場処理
// ============================================================
void enterState(DeviceState s) {
  if (s == gState) return;
  gState       = s;
  gForceRedraw = true;

  // 向きに合わせて表示を回転させ、常に正立表示
  // 実機で確認して合わない場合は回転値を調整してください (0〜3)
  switch (s) {
    case ST_IDLE:       M5.Display.setRotation(0); break; // 垂直・正面
    case ST_POMODORO:   M5.Display.setRotation(3); break; // 右に90° (1→3に修正)
    case ST_LONG_BREAK: M5.Display.setRotation(2); break; // 180°
    case ST_STATS:      M5.Display.setRotation(1); break; // 左に90° (3→1に修正)
    case ST_SETTINGS:   M5.Display.setRotation(0); break; // 水平
  }

  switch (s) {
    case ST_IDLE:
      gTimerActive = false;
      break;
    case ST_POMODORO:
      gPomPhase = PH_WORK;
      startTimer(WORK_SEC);
      break;
    case ST_LONG_BREAK:
      gTimerActive = false;
      startTimer(gLongBreakMin * 60);
      break;
    case ST_STATS:
    case ST_SETTINGS:
      gTimerActive = false;
      break;
  }
}

// ============================================================
//  描画: アイドル (時計 + 天気)
// ============================================================
void drawIdle() {
  M5.Display.fillScreen(C_IDLE_BG);
  M5.Display.setTextColor(C_IDLE_FG, C_IDLE_BG);

  time_t now = time(nullptr);
  struct tm* t = localtime(&now);

  // 時刻 (大)
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_24);
  char tbuf[6];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
  drawCenter(tbuf, 10);

  // 日付 (気温と同サイズ: 20px)
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
  const char* wd[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  char dbuf[16];
  snprintf(dbuf, sizeof(dbuf), "%d/%d %s", t->tm_mon+1, t->tm_mday, wd[t->tm_wday]);
  drawCenter(dbuf, 42);

  // 区切り線
  M5.Display.drawFastHLine(12, 62, 104, 0x334466);

  // 天気セクション
  if (gWeatherReady) {
    // アイコン (左寄り: x=30, y=90 が中心)
    drawWeatherIcon(gWeatherCode, 30, 90, 20);

    // 気温 (右寄り)
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
    M5.Display.setTextColor(C_IDLE_FG, C_IDLE_BG);
    char tbuf2[8];
    snprintf(tbuf2, sizeof(tbuf2), "%.0f°C", gWeatherTemp);
    M5.Display.setCursor(60, 78);
    M5.Display.print(tbuf2);

  } else {
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(0x445566, C_IDLE_BG);
    drawCenter("Loading...", 88);
  }

}

// ============================================================
//  描画: 円形タイマー (残り時間を扇形で表示)
// ============================================================
void drawCircleTimer(int remaining, int total, uint32_t colorFill, uint32_t colorBg) {
  const int cx   = 64;   // 中心X
  const int cy   = 64;   // 中心Y
  const int rOut = 58;   // 外径
  const int rIn  = 34;   // 内径 (ドーナツの穴)

  // 背景を塗りつぶし
  M5.Display.fillScreen(colorBg);

  // 経過時間の割合 (0.0〜1.0)
  float elapsed = (total > 0) ? (float)(total - remaining) / (float)total : 0.0f;
  elapsed = constrain(elapsed, 0.0f, 1.0f);

  // 12時の位置 = -90度。時計回りに elapsed × 360度 を塗る
  // lgfxのfillArcは (cx, cy, 外径, 内径, 開始角度, 終了角度, 色)
  float startAngle = -90.0f;
  float endAngle   = startAngle + elapsed * 360.0f;

  // トラック色: fill を1/3に暗くした色 (チャンネルごとに計算)
  uint8_t tr = ((colorFill >> 16) & 0xFF) / 3;
  uint8_t tg = ((colorFill >>  8) & 0xFF) / 3;
  uint8_t tb = ( colorFill        & 0xFF) / 3;
  uint32_t trackColor = ((uint32_t)tr << 16) | ((uint32_t)tg << 8) | tb;
  // 残り時間部分（薄い色＝未経過）
  M5.Display.fillArc(cx, cy, rOut, rIn, -90.0f, -90.0f + 360.0f, trackColor);
  // 経過済み部分（メインカラー）
  if (elapsed > 0.0f) {
    M5.Display.fillArc(cx, cy, rOut, rIn, startAngle, endAngle, colorFill);
  }

  // 12時位置の基準点（白い丸）
  const float rad0 = -90.0f * M_PI / 180.0f;
  int dotX = cx + (int)((rOut - 4) * cosf(rad0));
  int dotY = cy + (int)((rOut - 4) * sinf(rad0));
  M5.Display.fillCircle(dotX, dotY, 3, 0xFFFFFF);
}

// ============================================================
//  描画: ポモドーロ (集中 or 短い休憩)
// ============================================================
void drawPomodoro() {
  bool isWork  = (gPomPhase == PH_WORK);
  uint32_t bg  = isWork ? C_WORK_BG : C_SBRK_BG;
  uint32_t fg  = isWork ? C_WORK_FG : C_SBRK_FG;
  uint32_t bar = isWork ? C_BAR_WORK : C_BAR_BRK;
  int rem      = timerRemaining();
  int total    = isWork ? WORK_SEC : SHORT_BREAK_SEC;

  if (gCircleTimer) {
    // ---- 円形表示モード ----
    drawCircleTimer(rem, total, bar, bg);

    // フェーズラベル (ドーナツ中央に小さく)
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(fg, bg);
    drawCenter(isWork ? "FOCUS" : "BREAK", 52);


  } else {
    // ---- デジタル表示モード ----
    M5.Display.fillScreen(bg);
    M5.Display.setTextColor(fg, bg);

    // フェーズラベル
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_16);
    drawCenter(isWork ? "FOCUS" : "BREAK", 6);

    // タイマー (大)
    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", rem / 60, rem % 60);
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_24);
    drawCenter(tbuf, 30);

    // プログレスバー
    drawBar(rem, total, 54, bar);

  }

}

// ============================================================
//  描画: 長い休憩
// ============================================================
void drawLongBreak() {
  int rem   = timerRemaining();
  int total = gLongBreakMin * 60;
  char tbuf[6];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d", rem / 60, rem % 60);

  if (gCircleTimer) {
    // ---- 円形表示モード ----
    drawCircleTimer(rem, total, 0x3a80c0, C_LBRK_BG);

    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(C_LBRK_FG, C_LBRK_BG);
    drawCenter("LONG BREAK", 52);

  } else {
    // ---- デジタル表示モード ----
    M5.Display.fillScreen(C_LBRK_BG);
    M5.Display.setTextColor(C_LBRK_FG, C_LBRK_BG);

    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_16);
    drawCenter("LONG BREAK", 6);

    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_24);
    drawCenter(tbuf, 30);

    drawBar(rem, total, 54, 0x3A80C0);  // CIRCLEと同色
  }
}

// ============================================================
//  描画: 今日の統計
// ============================================================
void drawStats() {
  M5.Display.fillScreen(C_STAT_BG);
  M5.Display.setTextColor(C_STAT_FG, C_STAT_BG);

  // タイトル (気温と同サイズ: 20px, 明るいカラー)
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
  M5.Display.setTextColor(0x6644AA, C_STAT_BG);
  drawCenter("TODAY", 6);

  // 区切り線
  M5.Display.drawFastHLine(8, 30, 112, 0x332260);

  // セッション数 (大)
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_28);
  M5.Display.setTextColor(0xFFFFFF, C_STAT_BG);
  char pbuf[8];
  snprintf(pbuf, sizeof(pbuf), "%d", gTodayPom);
  drawCenter(pbuf, 36);

  // "sessions" ラベル (気温と同サイズ: 20px, 明るいカラー)
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
  M5.Display.setTextColor(0x6644AA, C_STAT_BG);
  drawCenter("sessions", 68);

  // 区切り線
  M5.Display.drawFastHLine(8, 90, 112, 0x332260);

  // 集中時間
  int min = gTodayFocusSec / 60;
  char mbuf[16];
  if (min >= 60) snprintf(mbuf, sizeof(mbuf), "%dh %dm", min / 60, min % 60);
  else           snprintf(mbuf, sizeof(mbuf), "%d min", min);
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
  M5.Display.setTextColor(0xFFFFFF, C_STAT_BG);
  drawCenter(mbuf, 96);

}

// ============================================================
//  描画: 設定モード
// ============================================================
// 設定画面のカーソル位置 (0=長い休憩時間, 1=タイマー表示モード)
int gSettCursor = 0;

void drawSettings() {
  M5.Display.fillScreen(C_SETT_BG);
  M5.Display.setTextColor(C_SETT_FG, C_SETT_BG);

  // タイトル
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_16);
  drawCenter("SETTINGS", 4);
  M5.Display.drawFastHLine(8, 22, 112, 0x444444);

  // --- 項目1: 長い休憩の時間 ---
  uint32_t c1 = (gSettCursor == 0) ? C_ACCENT : 0x777777;
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
  M5.Display.setTextColor(c1, C_SETT_BG);
  drawCenter("LONG BREAK", 28);
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_20);
  M5.Display.setTextColor(c1, C_SETT_BG);
  char vbuf[8];
  snprintf(vbuf, sizeof(vbuf), "%d min", gLongBreakMin);
  drawCenter(vbuf, 42);

  M5.Display.drawFastHLine(8, 68, 112, 0x333333);

  // --- 項目2: タイマー表示モード ---
  uint32_t c2 = (gSettCursor == 1) ? C_ACCENT : 0x777777;
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
  M5.Display.setTextColor(c2, C_SETT_BG);
  drawCenter("TIMER MODE", 74);
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_16);
  M5.Display.setTextColor(c2, C_SETT_BG);
  drawCenter(gCircleTimer ? "CIRCLE" : "DIGITAL", 90);

}

// ============================================================
//  日次リセット
// ============================================================
void checkDailyReset() {
  if (time(nullptr) < 100000) return; // NTP未同期
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  if (t->tm_mday != gLastResetDay) {
    gTodayPom      = 0;
    gTodayFocusSec = 0;
    gLastResetDay  = t->tm_mday;
    prefs.putInt("todayPom",      0);
    prefs.putInt("todayFocusSec", 0);
    prefs.putInt("lastResetDay",  gLastResetDay);
  }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Imu.init();

  M5.Display.setRotation(0);
  M5.Display.setBrightness(150);
  M5.Display.fillScreen(C_IDLE_BG);
  M5.Display.setTextColor(C_IDLE_FG, C_IDLE_BG);

  // 起動画面 - グラデーションロゴ (Orange #F0A030 → Crimson #C03060)
  {
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_24);
    const char* title = "POMOCORO";
    const int   len   = 8;

    // 全体幅を計測して中央揃えの開始X座標を計算
    int totalW = M5.Display.textWidth(title);
    int x      = (128 - totalW) / 2;
    int y      = 44;

    for (int i = 0; i < len; i++) {
      float t   = (float)i / (float)(len - 1);          // 0.0 → 1.0
      uint8_t r = (uint8_t)(0xF0 + (0xC0 - 0xF0) * t); // F0 → C0
      uint8_t g = (uint8_t)(0xA0 + (0x30 - 0xA0) * t); // A0 → 30
      uint8_t b = (uint8_t)(0x30 + (0x60 - 0x30) * t); // 30 → 60
      M5.Display.setTextColor(M5.Display.color888(r, g, b), C_IDLE_BG);
      M5.Display.setCursor(x, y);
      char ch[2] = { title[i], '\0' };
      M5.Display.print(ch);
      x += M5.Display.textWidth(ch);
    }

    // サブテキスト
    M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
    M5.Display.setTextColor(0xAABBCC, C_IDLE_BG);
    M5.Display.setCursor(8, 76);
    M5.Display.print("Connecting...");
  }

  // Preferences 読み込み
  prefs.begin("pomcoro", false);
  gLongBreakMin  = prefs.getInt("longBreakMin",  15);
  gTodayPom      = prefs.getInt("todayPom",       0);
  gTodayFocusSec = prefs.getInt("todayFocusSec",  0);
  gLastResetDay  = prefs.getInt("lastResetDay",   -1);
  gCircleTimer   = prefs.getBool("circleTimer",  false);

  // Wi-Fi 接続 (プライマリ → セカンダリの順で試行)
  struct { const char* ssid; const char* pass; } wifiList[] = {
    { WIFI_SSID_1, WIFI_PASS_1 },
    { WIFI_SSID_2, WIFI_PASS_2 },
  };

  bool wifiConnected = false;
  for (auto& w : wifiList) {
    if (strlen(w.ssid) == 0) continue;  // 未設定はスキップ
    M5.Display.setTextColor(0xAABBCC, C_IDLE_BG);
    M5.Display.setCursor(8, 82);
    M5.Display.printf("%.16s...", w.ssid); // SSID名を最大16文字表示
    WiFi.disconnect(true);
    WiFi.begin(w.ssid, w.pass);
    for (int i = 0; i < WIFI_TIMEOUT_SEC * 2 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      break;  // 接続成功したらループを抜ける
    }
  }

  if (wifiConnected) {
    M5.Display.setTextColor(0xAABBCC, C_IDLE_BG);
    M5.Display.setCursor(8, 98);
    M5.Display.print("Syncing NTP...");
    // configTzTime でタイムゾーンを直接指定 (JST-9 = UTC+9)
    configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp", "pool.ntp.org");
    // NTP完了まで最大15秒待機 (tm_year が 70=1970 より大きくなれば同期済み)
    unsigned long ntpStart = millis();
    while (millis() - ntpStart < 15000UL) {
      time_t now = time(nullptr);
      struct tm ti;
      if (localtime_r(&now, &ti) && ti.tm_year > 70) break;
      delay(200);
    }
    fetchWeather();
  } else {
    M5.Display.setTextColor(0xAABBCC, C_IDLE_BG);
    M5.Display.setCursor(8, 98);
    M5.Display.print("Offline Mode");
    delay(1500);
  }

  // 日次リセット確認
  checkDailyReset();

  gState = ST_IDLE;
  gForceRedraw = true;
}

// ============================================================
//  loop()
// ============================================================
void loop() {
  M5.update();
  unsigned long now = millis();

  // ---- 方向検出 & デバウンス ----
  Orientation ori = detectOrientation();

  if (ori != gCurOri) {
    gCurOri   = ori;
    gOriSince = now;
  }

  if (ori != ORI_UNKNOWN &&
      ori == gCurOri &&
      ori != gStableOri &&
      (now - gOriSince) >= ORI_DEBOUNCE_MS) {
    gStableOri = ori;
    enterState(ORI_TO_STATE[ori]);
  }

  // ---- ボタン: 各画面のタップ操作 ----
  // 設定画面のみ長押し/短押しを区別するため wasReleased で判定
  // 他の画面は押した瞬間 (wasPressed) に即反応
  if (gState == ST_SETTINGS) {
    if (M5.BtnA.wasReleased()) {
      if (M5.BtnA.wasReleaseFor(600)) {
        // 長押し: カーソル移動 (0=長い休憩時間 → 1=タイマー表示モード → 0)
        gSettCursor = (gSettCursor + 1) % 2;
      } else {
        // 短押し: 選択中の項目の値を変更
        if (gSettCursor == 0) {
          gLongBreakMin = (gLongBreakMin == 15) ? 20 : 15;
          prefs.putInt("longBreakMin", gLongBreakMin);
        } else {
          gCircleTimer = !gCircleTimer;
          prefs.putBool("circleTimer", gCircleTimer);
        }
      }
      gForceRedraw = true;
    }
  } else if (M5.BtnA.wasPressed()) {
    switch (gState) {

      case ST_IDLE:
        // ホーム画面: 天気を今すぐ手動更新
        if (WiFi.status() == WL_CONNECTED) {
          fetchWeather();
        }
        gForceRedraw = true;
        break;

      case ST_POMODORO:
        // タイマー画面: 現在のフェーズをリスタート
        if (gPomPhase == PH_WORK) {
          startTimer(WORK_SEC);
        } else {
          startTimer(SHORT_BREAK_SEC);
        }
        gForceRedraw = true;
        break;

      case ST_LONG_BREAK:
        // 休憩画面: 休憩を早切りして集中セッション開始
        gTimerActive = false;
        gPomPhase    = PH_WORK;
        startTimer(WORK_SEC);
        gState       = ST_POMODORO;
        M5.Display.setRotation(3);
        gForceRedraw = true;
        break;

      case ST_STATS:
        // 記録画面: 記録をその場でクリア
        gTodayPom      = 0;
        gTodayFocusSec = 0;
        prefs.putInt("todayPom",      0);
        prefs.putInt("todayFocusSec", 0);
        gForceRedraw = true;
        break;
    }
  }

  // ---- タイマー完了処理 ----
  if (!gAlertShown && timerDone()) {
    gAlertShown = true;

    if (gState == ST_POMODORO) {
      if (gPomPhase == PH_WORK) {
        // 集中完了 → 統計更新 → 短い休憩へ
        gTodayPom++;
        gTodayFocusSec += WORK_SEC;
        prefs.putInt("todayPom",      gTodayPom);
        prefs.putInt("todayFocusSec", gTodayFocusSec);
        flashAlert(0x44FF88, 0x001a08);   // 緑フラッシュ (明→暗交互)
        gPomPhase = PH_SHORT_BREAK;
        startTimer(SHORT_BREAK_SEC);
      } else {
        // 短い休憩完了 → 次の集中へ
        flashAlert(0xFF4444, 0x1a0000);   // 赤フラッシュ (明→暗交互)
        gPomPhase = PH_WORK;
        startTimer(WORK_SEC);
      }
      gForceRedraw = true;

    } else if (gState == ST_LONG_BREAK) {
      flashAlert(0x44AAFF, 0x00081a);   // 青フラッシュ (明→暗交互)
      gForceRedraw = true;
    }
  }

  // ---- 天気定期更新 ----
  if (WiFi.status() == WL_CONNECTED &&
      (now - gLastWeather) >= WEATHER_INTERVAL_MS) {
    fetchWeather();
    if (gState == ST_IDLE) gForceRedraw = true;
  }

  // ---- 日次リセット ----
  checkDailyReset();

  // ---- 画面更新 ----
  // 秒が変わった or 強制更新 or アイドル時10秒ごと
  int  secLeft  = timerRemaining();
  bool needDraw = gForceRedraw ||
                  (gTimerActive && secLeft != gPrevSecLeft) ||
                  (gState == ST_IDLE && (now - gLastDraw) >= 10000UL);

  if (needDraw) {
    gForceRedraw = false;
    gPrevSecLeft = secLeft;
    gLastDraw    = now;

    switch (gState) {
      case ST_IDLE:       drawIdle();       break;
      case ST_POMODORO:   drawPomodoro();   break;
      case ST_LONG_BREAK: drawLongBreak();  break;
      case ST_STATS:      drawStats();      break;
      case ST_SETTINGS:   drawSettings();   break;
    }
  }

  delay(50);
}
