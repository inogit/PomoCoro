/*
 * PomoCoro - IMUキャリブレーション用スケッチ
 * ============================================================
 * PomoCoro.ino を書き込む前に、このスケッチを先に実行してください。
 *
 * 使い方:
 *   1. このスケッチを ATOMS3 に書き込む
 *   2. シリアルモニタを開く (115200bps)
 *   3. ATOMS3 を各向きにしてシリアル出力を確認する
 *
 * 確認する向きと、PomoCoro.ino の設定値の対応:
 *
 *   [向き1] 垂直に立て、USBを下にして画面を自分に向ける
 *           → ay の符号を確認
 *           → ay > 0 なら UPRIGHT_AXIS_SIGN = +1
 *           → ay < 0 なら UPRIGHT_AXIS_SIGN = -1
 *
 *   [向き2] 右に90°転がす (右側が下)
 *           → ax の符号を確認
 *           → ax > 0 なら RIGHT_AXIS_SIGN = +1
 *           → ax < 0 なら RIGHT_AXIS_SIGN = -1
 *
 *   [向き3] 水平に置く (画面を上向き)
 *           → az が ±1 に近ければ正常
 *
 *   [向き4] 逆さまに立てる (USBを上)
 *           → ay が向き1と逆符号になること確認
 *
 *   [向き5] 左に90°転がす (左側が下)
 *           → ax が向き2と逆符号になること確認
 *
 * 確認した値を PomoCoro.ino の以下の行に反映してください:
 *   static const int UPRIGHT_AXIS_SIGN = +1;  // (または -1)
 *   static const int RIGHT_AXIS_SIGN   = -1;  // (または +1)
 * ============================================================
 */

#include <M5Unified.h>

void setup() {
  Serial.begin(115200);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Imu.init();

  M5.Display.setRotation(0);
  M5.Display.setBrightness(150);
  M5.Display.fillScreen(0x001020);
  M5.Display.setTextColor(TFT_WHITE, 0x001020);
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
  M5.Display.setCursor(4, 4);
  M5.Display.print("IMUキャリブ中");
  M5.Display.setCursor(4, 24);
  M5.Display.print("シリアルモニタ");
  M5.Display.setCursor(4, 40);
  M5.Display.print("115200bps");

  Serial.println("=== PomoCoro IMUキャリブレーション ===");
  Serial.println("ATOMS3を各向きに傾けて ax/ay/az を確認してください");
  Serial.println("---");
  delay(1000);
}

void loop() {
  float ax, ay, az;
  M5.Imu.getAccel(&ax, &ay, &az);

  // 各軸の値を出力
  Serial.printf("ax=%+.2f  ay=%+.2f  az=%+.2f", ax, ay, az);

  // どの軸が支配的かを判定して向きを推定
  float aax = fabsf(ax), aay = fabsf(ay), aaz = fabsf(az);
  float dom = max({aax, aay, aaz});

  Serial.print("  →  ");
  if (aaz == dom && aaz > 0.75f) {
    Serial.print("水平置き (Z支配的)");
  } else if (aay == dom && aay > 0.60f) {
    if (ay > 0) Serial.print("Y+  (垂直? or 逆さま?)");
    else        Serial.print("Y-  (逆さま? or 垂直?)");
  } else if (aax == dom && aax > 0.60f) {
    if (ax > 0) Serial.print("X+  (左に倒した? or 右に倒した?)");
    else        Serial.print("X-  (右に倒した? or 左に倒した?)");
  } else {
    Serial.print("遷移中 / 判定なし");
  }
  Serial.println();

  // 液晶にも表示
  M5.Display.fillScreen(0x001020);
  M5.Display.setFont(&lgfx::fonts::lgfxJapanGothic_12);
  M5.Display.setTextColor(TFT_WHITE, 0x001020);

  char buf[20];
  M5.Display.setCursor(4, 4);
  snprintf(buf, sizeof(buf), "ax=%+.2f", ax);
  M5.Display.print(buf);

  M5.Display.setCursor(4, 24);
  snprintf(buf, sizeof(buf), "ay=%+.2f", ay);
  M5.Display.print(buf);

  M5.Display.setCursor(4, 44);
  snprintf(buf, sizeof(buf), "az=%+.2f", az);
  M5.Display.print(buf);

  // 色でどの軸が支配的か表示
  if (aaz == dom && aaz > 0.75f) {
    M5.Display.fillRect(0, 70, 128, 20, 0x004400);
    M5.Display.setCursor(4, 72);
    M5.Display.print("水平 (設定モード)");
  } else if (aay == dom) {
    uint32_t c = (ay > 0) ? 0x000066 : 0x440044;
    M5.Display.fillRect(0, 70, 128, 20, c);
    M5.Display.setCursor(4, 72);
    M5.Display.print(ay > 0 ? "Y+  垂直系" : "Y-  垂直系");
  } else if (aax == dom) {
    uint32_t c = (ax > 0) ? 0x664400 : 0x006600;
    M5.Display.fillRect(0, 70, 128, 20, c);
    M5.Display.setCursor(4, 72);
    M5.Display.print(ax > 0 ? "X+  左右系" : "X-  左右系");
  }

  delay(300);
}
