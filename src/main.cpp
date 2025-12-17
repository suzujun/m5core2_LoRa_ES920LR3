#include <M5Unified.h>

// @see https://ikkei.akiba.co.jp/ikkei_Electronics/M5LR3.html
// #define RX_pin 13 // ES920LR3 TX 接続ピン
// #define TX_pin 14 // ES920LR3 RX 接続ピン
// GROVE PORT.A ピン定義
// ESP32のRX_pin = ES920LR3のTXに接続
// ESP32のTX_pin = ES920LR3のRXに接続
#define RX_pin 33 // GROVE PORT.A RX (ESP32側) - ES920LR3のTXに接続
#define TX_pin 32 // GROVE PORT.A TX (ESP32側) - ES920LR3のRXに接続

int boot_pin = 22;
int reset_pin = 19;

// センサーデータ構造体（12バイト）
// パディングを避けるため、packed属性を使用
struct __attribute__((packed)) SensorData {
  uint8_t nodeId;          // Byte 0: 0-10
  uint16_t windDirection;  // Byte 1-2: 0-360 (度そのまま)
  uint16_t airSpeed100;    // Byte 3-4: 0-5000 (値×100)
  uint16_t virtualTemp100; // Byte 5-6: 0-5000 (値×100)
  uint8_t rssiAbs;         // Byte 7: 0-99 (-rssiの絶対値)
  // uint32_t unixmilli;      // Byte 8-11: 0-864000000
};

// Serial1をGROVE PORT.A（GPIO32/33）に割り当ててES920LR3と通信
// Serial2はM-BUSで使用されているため、Serial1を使用

// ---- ここに自分の LoRaWAN パラメータを入れる ----
// すべて 16進数文字列 (大文字 or 小文字どちらでもOKなことが多い)
const char *DEV_EUI = "0100010002000700";                 // 例: メーカー提供 DevEUI
const char *APP_EUI = "0100010001000100";                 // 例: 自分で決めた JoinEUI/AppEUI
const char *APP_KEY = "010001000100010001000000FFFFFFFF"; // 例: 自分で生成した 128bit鍵
// --------------------------------------------------

void LoRa_Reset() {
  pinMode(reset_pin, OUTPUT);
  digitalWrite(reset_pin, LOW); // NRST "L"
  delay(10);
  pinMode(reset_pin, INPUT); // NRST open
  delay(1);                  // firmware_updaterと同様に1msに変更
}

// ES920LR3コマンド送信関数
// 参考: ES920LR3_LoRaWAN_コマンド仕様ソフトウェア説明書_1.01.pdf
// m5core2_firmware_updaterのCommandWithResponseを参考に実装
String sendCommand(const String &cmd, uint32_t wait_ms = 1000) {
  // 受信バッファをクリア
  while (Serial1.available()) {
    Serial1.read();
  }

  // コマンド送信（CR+LF付き）
  Serial1.print(cmd);
  Serial1.print("\r\n");
  Serial1.flush(); // 送信完了を待つ

  Serial.print("[TX] ");
  Serial.println(cmd);

  // レスポンスを待つ
  // Serial1.setTimeout(2000)が設定されているので、readString()は最大2秒待つ
  delay(200); // コマンド送信後の待機時間を延長（100ms → 200ms）

  String resp = "";
  uint32_t start = millis();

  // タイムアウトまでデータを待つ
  while (millis() - start < wait_ms) {
    if (Serial1.available()) {
      // 1文字ずつ読み取ってリアルタイムで表示
      while (Serial1.available()) {
        char c = Serial1.read();
        Serial.write(c); // リアルタイムで表示
        resp += c;
      }
      // データを受信したら少し待って追加データを確認
      delay(100);
    } else {
      delay(50);
    }
  }

  // 最後に残っているデータを読み取る
  delay(100);
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c);
    resp += c;
  }

  if (resp.length() > 0) {
    Serial.print("[RX] ");
    Serial.print(resp.length());
    Serial.print(" bytes: ");
    // 改行を可視化
    String displayResp = resp;
    displayResp.replace("\r", "\\r");
    displayResp.replace("\n", "\\n");
    Serial.println(displayResp);
  } else {
    Serial.println("[RX] (no response)");
  }

  return resp;
}

// コマンド応答がOKかチェック
bool checkCommandOK(const String &response) {
  if (response.length() == 0) {
    return false;
  }
  String upperResp = response;
  upperResp.toUpperCase();
  upperResp.trim();

  // OKが含まれ、NGが含まれない場合
  if (upperResp.indexOf("OK") >= 0 && upperResp.indexOf("NG") == -1) {
    return true;
  }
  return false;
}

// 送信結果の状態を表すenum
enum SendResult {
  SEND_SUCCESS = 0,    // 送信成功
  SEND_FAILURE = 1,    // 送信失敗
  SEND_WAIT = 2,       // 送信待ち（NG 102など）
  SEND_SELECT_MODE = 3 // 再起動時の応答
};

SendResult checkSendSuccess(const String &response) {
  if (response.indexOf("Select Mode [") >= 0) {
    return SEND_SELECT_MODE;
  }

  // モジュールの仕様に合わせて成功パターンを調整してください
  String upperResp = response;
  upperResp.toUpperCase();

  // "NG 102" を検出（送信待ち状態）
  if (upperResp.indexOf("NG 102") >= 0 || upperResp.indexOf("NG102") >= 0) {
    return SEND_WAIT;
  }

  if (upperResp.indexOf("OK") >= 0 ||
      upperResp.indexOf("SUCCESS") >= 0 ||
      upperResp.indexOf("SEND OK") >= 0) {
    return SEND_SUCCESS;
  }

  if (upperResp.indexOf("FAIL") >= 0 ||
      upperResp.indexOf("ERROR") >= 0 ||
      upperResp.indexOf("DENY") >= 0 ||
      upperResp.indexOf("NG") >= 0) {
    return SEND_FAILURE;
  }

  // レスポンスがない場合は失敗とみなす
  return SEND_FAILURE;
}

bool waitForJoinOK(uint32_t timeout_ms = 30000) {
  // 参考: ES920LR3仕様書 - startコマンド後のJoin応答
  Serial.println("Waiting for JOIN response...");
  Serial.print("Timeout: ");
  Serial.print(timeout_ms / 1000);
  Serial.println(" seconds");

  uint32_t start = millis();
  String buf;
  uint32_t lastPrint = 0;

  while (millis() - start < timeout_ms) {
    // 5秒ごとに経過時間を表示
    if (millis() - lastPrint > 5000) {
      Serial.print("[JOIN] Waiting... ");
      Serial.print((millis() - start) / 1000);
      Serial.println("s elapsed");
      lastPrint = millis();
    }

    while (Serial1.available()) {
      char c = Serial1.read();
      Serial.write(c); // リアルタイムで表示（デバッグ出力）
      buf += c;

      // バッファが長くなりすぎないように制限
      if (buf.length() > 200) {
        buf = buf.substring(buf.length() - 100);
      }
    }

    // 応答を解析
    if (buf.length() > 0) {
      String upperBuf = buf;
      upperBuf.toUpperCase();
      String trimmedBuf = buf;
      trimmedBuf.trim();

      // Join成功を確認
      // 仕様書: "JOIN" - Over The Air Activation で Join-Accept を受信した際に出力します。
      if (upperBuf.indexOf("JOIN") >= 0 && upperBuf.indexOf("NG") == -1) {
        Serial.print("\n[JOIN_SUCCESS] Join completed. Response: ");
        Serial.println(trimmedBuf);
        return true;
      }
      // NG応答を確認（Join失敗）
      if (upperBuf.indexOf("NG") >= 0) {
        Serial.print("\n[JOIN_FAILED] Join failed. Response: ");
        Serial.println(trimmedBuf);
        return false;
      }
    }

    delay(10);
  }

  Serial.print("\n[JOIN_TIMEOUT] Join timeout after ");
  Serial.print(timeout_ms / 1000);
  Serial.println(" seconds");
  if (buf.length() > 0) {
    Serial.print("Last response: ");
    Serial.println(buf);
  } else {
    Serial.println("No response received.");
  }
  return false;
}

void setup() {
  auto cfg = M5.config();
  // PORT.AのI2C機能を無効化（GPIO32/33をUARTとして使用するため）
  cfg.external_rtc = false; // RTC機能を無効化（必要に応じて）
  M5.begin(cfg);
  Serial.begin(115200);
  delay(2000);

  // M-BUS接続時の干渉を避けるため、ES920LR3初期化前にSerial2を無効化
  // ULSA M5BがSerial2を使用している場合、一時的に無効化する
  Serial.println("Disabling Serial2 to avoid interference with ES920LR3 initialization");
  Serial2.end(); // Serial2を無効化
  delay(100);

  Serial.println("M5Stack Core2 + ES920LR3 LoRaWAN test");

  // LCD初期化とタイトル表示（最初に実行）
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 10);
  M5.Display.println("LoRaWAN Stats");
  M5.Display.drawLine(0, 35, 320, 35, WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 50);
  M5.Display.println("Initializing...");

  Serial.print("Initializing LoRa serial: RX=");
  Serial.print(RX_pin);
  Serial.print(", TX=");
  Serial.println(TX_pin);

  // モジュールをリセットして設定モードに入る
  // NG 102エラー = オペレーションモードの送信待ち状態
  // モジュールが設定モードに入れていないため、強制的に設定モードに入る
  Serial.println("\n=== Initializing ES920LR3 Module ===");
  Serial.println("Forcing module into configuration mode...");

  // M-BUS接続時の干渉を確認
  Serial.println("\n=== Checking GPIO pin states ===");
  Serial.print("boot_pin (GPIO22) before setup: ");
  pinMode(boot_pin, INPUT_PULLUP);
  Serial.println(digitalRead(boot_pin) ? "HIGH" : "LOW");
  Serial.print("reset_pin (GPIO19) before setup: ");
  pinMode(reset_pin, INPUT_PULLUP);
  Serial.println(digitalRead(reset_pin) ? "HIGH" : "LOW");

  // boot_pinを確実に制御するため、まずINPUT_PULLUPからOUTPUTに変更
  pinMode(boot_pin, OUTPUT);
  digitalWrite(boot_pin, LOW); // まずLOWに設定（確実に制御）
  delay(50);
  digitalWrite(boot_pin, HIGH); // boot mode (設定モードに入る)
  Serial.print("boot_pin set to HIGH, current state: ");
  Serial.println(digitalRead(boot_pin) ? "HIGH" : "LOW");
  delay(100); // boot_pin設定後の待機時間（重要）

  // リセット実行
  Serial.println("Performing reset...");
  LoRa_Reset();

  // モジュールの起動を待つ（ULSA M5B接続時は起動に時間がかかる可能性）
  // ES920LR3の仕様書によると、リセット後の起動に時間がかかる
  Serial.println("Waiting for module to boot...");
  delay(500); // リセット後の待機時間を延長（200ms → 500ms）

  // Serial1をGROVE PORT.A（GPIO32/33）に割り当ててES920LR3と通信
  // ULSA M5BはSerial2を使用しているため、Serial1を使用
  Serial1.end(); // 既存のシリアルポートを確実に終了
  delay(50);     // シリアル終了後の待機時間

  // タイムアウトを延長（モジュールの応答を待つため）
  Serial1.setTimeout(2000); // 50ms → 2000msに延長

  Serial.println("Initializing Serial1 (115200bps, 8N1)...");
  Serial1.begin(115200, SERIAL_8N1, RX_pin, TX_pin);

  // シリアル初期化後の待機時間
  delay(300); // シリアル初期化後の待機時間を延長

  // モジュール起動時のプロンプトを待つ
  // ES920LR3は設定モードに入ると「Select Mode [1.terminal or 2.processor]」を表示
  Serial.println("Waiting for module startup prompt...");
  Serial.println("Expected: Select Mode [1.terminal or 2.processor]");
  String startupPrompt = "";
  uint32_t promptStart = millis();
  const uint32_t promptTimeout = 3000; // 3秒でタイムアウト

  while (millis() - promptStart < promptTimeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      Serial.write(c); // リアルタイムで表示
      startupPrompt += c;

      // 「Select Mode」が含まれていれば起動完了
      if (startupPrompt.indexOf("Select Mode") >= 0 ||
          startupPrompt.indexOf("select mode") >= 0 ||
          startupPrompt.indexOf("SELECT MODE") >= 0) {
        Serial.println("\n[OK] Module startup prompt received");
        break;
      }
    }
    delay(10);
  }

  if (startupPrompt.length() == 0) {
    Serial.println("\n[WARNING] No startup prompt received");
    Serial.println("Module may not be in configuration mode");
  } else {
    Serial.print("Startup prompt: ");
    Serial.println(startupPrompt);
  }

  // 残りのバッファをクリア
  delay(100);
  while (Serial1.available()) {
    int data = Serial1.read();
    if (data != -1) {
      Serial.write(data);
    }
  }

  // 1) 設定モードに入る（プロセッサーモード選択）
  // 参考: ES920LR3仕様書 - モード選択
  Serial.println("\n=== Step 1: Enter Configuration Mode ===");
  Serial.println("Selecting processor mode (2)...");
  Serial.print("boot_pin state: ");
  Serial.println(digitalRead(boot_pin) ? "HIGH" : "LOW");

  // 「2」コマンドを複数回試行（モジュールが応答するまで）
  String modeResp = "";
  int retryCount = 0;
  const int maxRetries = 5;

  while (modeResp.length() == 0 && retryCount < maxRetries) {
    retryCount++;
    Serial.print("Attempt ");
    Serial.print(retryCount);
    Serial.print("/");
    Serial.println(maxRetries);

    // boot_pinがHIGHであることを確認
    if (!digitalRead(boot_pin)) {
      Serial.println("[WARNING] boot_pin is LOW, setting to HIGH");
      digitalWrite(boot_pin, HIGH);
      delay(100);
    }

    modeResp = sendCommand("2", 2000);

    if (modeResp.length() == 0) {
      Serial.println("[WARNING] No response, retrying...");
      delay(500); // 再試行前の待機時間
    }
  }

  // 設定モードに入ったら、boot_pinをLOWに戻す（normal mode）
  if (modeResp.length() > 0) {
    Serial.println("[OK] Module responded to '2' command");
    Serial.print("Response: ");
    Serial.println(modeResp);
    digitalWrite(boot_pin, LOW); // normal modeに戻す
    delay(100);
  } else {
    Serial.println("[ERROR] No response to '2' command after multiple attempts");
    Serial.println("Module may be stuck in operation mode");
    Serial.println("Try power cycling the module");
    // boot_pinはHIGHのまま維持（設定モードに入るため）
  }

  delay(500);

  // 2) 疎通確認（versionコマンドで確認）
  // firmware_updaterでは "v" コマンドを使用
  Serial.println("\n=== Step 2: Module Communication Test ===");
  String verResp;
  bool moduleResponded = false;

  for (int i = 0; i < 3; i++) {
    Serial.print("Attempt ");
    Serial.print(i + 1);
    Serial.println("/3");

    // firmware_updaterと同様に "v" コマンドを使用
    verResp = sendCommand("v", 2000);
    if (verResp.length() > 0) {
      // NGが含まれていないことを確認
      String upperResp = verResp;
      upperResp.toUpperCase();
      if (upperResp.indexOf("NG") == -1) {
        moduleResponded = true;
        Serial.println("[OK] Module responded!");
        Serial.print("Version: ");
        Serial.println(verResp);
        break;
      }
    }
    delay(500);
  }

  if (!moduleResponded) {
    Serial.println("\n[ERROR] No response from module after 3 attempts.");
    Serial.println("Possible causes:");
    Serial.println("  1. Wiring issue: Check RX/TX connections");
    Serial.println("  2. Power issue: Ensure module is powered");
    Serial.println("  3. Wrong pins: Verify GPIO32/33 (GROVE PORT.A) connections");
    Serial.println("  4. Baud rate: Module might use different baud rate");
    Serial.println("  5. Module not ready: Try power cycling");

    M5.Display.setCursor(10, 70);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("No module response!");
    M5.Display.setCursor(10, 90);
    M5.Display.setTextSize(1);
    M5.Display.println("Check wiring/power");
    M5.Display.setTextColor(WHITE, BLACK);

    while (true) {
      M5.update();
      delay(1000);
    }
  }

  // // 設定確認（loadコマンド）
  // Serial.println("\n=== Step 2.1: Load Configuration ===");
  // String loadResp = sendCommand("load", 1000);
  // if (!checkCommandOK(loadResp)) {
  //   Serial.println("[WARNING] Load configuration failed");
  // }
  // delay(500);

  // 3) LoRaWAN Class設定
  // 参考: ES920LR3仕様書 8.1. class コマンド
  Serial.println("\n=== Step 3: LoRaWAN Class Setup ===");
  String classResp = sendCommand("class 1", 1000);
  if (!checkCommandOK(classResp)) {
    Serial.println("[ERROR] Class A setting failed!");
    M5.Display.setCursor(10, 90);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("Class A FAILED!");
    M5.Display.setTextColor(WHITE, BLACK);
    while (true) {
      M5.update();
      delay(1000);
    }
  }
  delay(500);

  // 4) デバイスID / 鍵設定
  // 参考: ES920LR3仕様書 8.4-8.6
  Serial.println("\n=== Step 4: Device Credentials Setup ===");
  String cmd;
  bool configOK = true;

  // DevEUI設定（16進数16文字）
  cmd = "deveui " + String(DEV_EUI);
  Serial.print("Setting DevEUI: ");
  Serial.println(DEV_EUI);
  String deveuiResp = sendCommand(cmd, 1000);
  if (!checkCommandOK(deveuiResp)) {
    Serial.println("[ERROR] DevEUI setting failed!");
    configOK = false;
  }
  delay(500);

  // AppEUI設定（16進数16文字）
  cmd = "appeui " + String(APP_EUI);
  Serial.print("Setting AppEUI: ");
  Serial.println(APP_EUI);
  String appeuiResp = sendCommand(cmd, 1000);
  if (!checkCommandOK(appeuiResp)) {
    Serial.println("[ERROR] AppEUI setting failed!");
    configOK = false;
  }
  delay(500);

  // AppKey設定（16進数32文字）
  cmd = "appkey " + String(APP_KEY);
  Serial.print("Setting AppKey: ");
  Serial.println(APP_KEY);
  String appkeyResp = sendCommand(cmd, 1000);
  if (!checkCommandOK(appkeyResp)) {
    Serial.println("[ERROR] AppKey setting failed!");
    configOK = false;
  }
  delay(500);

  // datarate設定（16進数32文字）
  // cmd = "datarate 3"; // DR2 帯域幅 125kHz 拡散率 10
  cmd = "datarate 6"; // DR5 帯域幅 125kHz 拡散率 7
  Serial.print("Setting datarate: 6");
  String datarateResp = sendCommand(cmd, 1000);
  if (!checkCommandOK(datarateResp)) {
    Serial.println("[ERROR] Datarate setting failed!");
    configOK = false;
  }
  delay(500);

  if (!configOK) {
    Serial.println("[ERROR] Configuration failed. Check parameters.");
    M5.Display.setCursor(10, 90);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("Config FAILED!");
    M5.Display.setTextColor(WHITE, BLACK);
    while (true) {
      M5.update();
      delay(1000);
    }
  }

  // 設定確認（showコマンド）
  Serial.println("\n=== Configuration Verification ===");
  String showResp = sendCommand("show", 2000);
  Serial.println("Current configuration:");
  Serial.println(showResp);
  delay(500);

  // 設定を保存
  // 参考: ES920LR3仕様書 8.22. save コマンド
  Serial.println("\n=== Saving Configuration ===");
  String saveResp = sendCommand("save", 1000);
  if (!checkCommandOK(saveResp)) {
    Serial.println("[WARNING] Save command response unclear");
  }
  delay(500);

  // 5) OTAA Join 開始
  // 参考: ES920LR3仕様書 8.25. start コマンド
  // startコマンドでオペレーションモードに移行し、OTAA Joinが開始される
  Serial.println("\n=== Step 5: OTAA Join ===");
  M5.Display.setCursor(10, 70);
  M5.Display.println("Joining...");
  Serial.println("Sending start command (entering operation mode)...");
  String startResp = sendCommand("start", 2000);

  // startコマンドのOKレスポンスを確認
  if (!checkCommandOK(startResp)) {
    Serial.println("[ERROR] Start command failed!");
    Serial.print("Response: ");
    Serial.println(startResp);
    M5.Display.setCursor(10, 90);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("Start FAILED!");
    M5.Display.setTextColor(WHITE, BLACK);
    while (true) {
      M5.update();
      delay(1000);
    }
  }

  Serial.println("Start command OK. Waiting for Join response...");

  // Join 完了待ち（モジュールの応答メッセージは仕様書に合わせてパターンを書き換え）
  bool joined = waitForJoinOK(30 * 60 * 1000);
  if (!joined) {
    Serial.println("Join failed. Stop here.");
    M5.Display.setCursor(10, 90);
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("Join FAILED!");
    M5.Display.setTextColor(WHITE, BLACK);
    while (true) {
      M5.update();
      delay(1000);
    }
  }

  Serial.println("Start uplink loop...");
  M5.Display.fillRect(0, 50, 320, 50, BLACK);
  M5.Display.setCursor(10, 50);
  M5.Display.setTextColor(GREEN, BLACK);
  M5.Display.println("Joined! Ready to send.");
  M5.Display.setTextColor(WHITE, BLACK);
}

void updateDisplay(uint32_t sendCount, uint32_t successCount, uint32_t failCount, bool lastSuccess, uint32_t elapsedMs) {
  // 統計情報表示エリアをクリア（タイトルと線の下）
  M5.Display.fillRect(0, 40, 320, 200, BLACK);

  // 最新の送信結果を表示
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 45);
  M5.Display.print("Last: ");
  if (lastSuccess) {
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.println("SUCCESS");
  } else {
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("FAILED");
  }
  M5.Display.setTextColor(WHITE, BLACK);

  // 前回送信からの経過時間を表示
  M5.Display.setTextSize(1);
  M5.Display.setCursor(10, 70);
  M5.Display.print("Elapsed: ");
  if (elapsedMs < 1000) {
    M5.Display.print(elapsedMs);
    M5.Display.println("ms");
  } else {
    M5.Display.print(elapsedMs / 1000);
    M5.Display.print(".");
    M5.Display.print((elapsedMs % 1000) / 100);
    M5.Display.println("s");
  }

  // 送信回数
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 90);
  M5.Display.print("Total: ");
  M5.Display.println(sendCount);

  // 成功回数（緑色）
  M5.Display.setTextColor(GREEN, BLACK);
  M5.Display.setCursor(10, 120);
  M5.Display.print("Success: ");
  M5.Display.println(successCount);
  M5.Display.setTextColor(WHITE, BLACK);

  // 失敗回数（赤色）
  M5.Display.setTextColor(RED, BLACK);
  M5.Display.setCursor(10, 150);
  M5.Display.print("Failed: ");
  M5.Display.println(failCount);
  M5.Display.setTextColor(WHITE, BLACK);

  // 成功率
  M5.Display.setTextSize(2);
  M5.Display.setCursor(10, 180);
  M5.Display.print("Rate: ");
  if (sendCount > 0) {
    uint32_t rate = (successCount * 100) / sendCount;
    if (rate >= 80) {
      M5.Display.setTextColor(GREEN, BLACK);
    } else if (rate >= 50) {
      M5.Display.setTextColor(YELLOW, BLACK);
    } else {
      M5.Display.setTextColor(RED, BLACK);
    }
    M5.Display.print(rate);
    M5.Display.println("%");
  } else {
    M5.Display.println("0%");
  }
  M5.Display.setTextColor(WHITE, BLACK);
}

void loop() {
  M5.update(); // M5Unifiedの更新処理

  static uint32_t lastSendTime = 0;
  static uint32_t sendCount = 0;
  static uint32_t successCount = 0;
  static uint32_t failCount = 0;
  static bool lastSuccess = false;

  // 前回送信からの経過時間を計算
  uint32_t elapsedMs = (lastSendTime > 0) ? (millis() - lastSendTime) : 0;

  // lastSendTimeから10秒経過している場合に送信可能
  bool canSend = (lastSendTime == 0) || (millis() - lastSendTime >= 10000);

  // 送信可能な場合のみ送信を試みる
  if (!canSend) {
    return;
  }

  // センサーデータをバイナリ形式で送信（12バイト）
  SensorData sensorData;

  // データの設定（例：実際のセンサー値に置き換えてください）
  sensorData.nodeId = 1;            // 0-10
  sensorData.windDirection = 180;   // 0-360 (度)
  sensorData.airSpeed100 = 123;     // 0-5000 (値×100、例: 12.3 m/s)
  sensorData.virtualTemp100 = 2025; // 0-5000 (値×100、例: 20.25°C)
  sensorData.rssiAbs = 45;          // 0-99 (-rssiの絶対値)
  // sensorData.unixmilli = millis() % 864000000UL; // 0-864000000 (ミリ秒、約10日間)

  // バイナリデータを送信（12バイト）
  Serial1.write((uint8_t *)&sensorData, sizeof(SensorData));
  Serial1.print("\r\n");
  Serial1.flush();
  lastSendTime = millis(); // 送信時刻を更新
  sendCount++;

  // デバッグ用：送信データを16進数で表示
  Serial.println("----------------------------------------");
  Serial.print("[SEND #");
  Serial.print(sendCount);
  Serial.print("] Payload (");
  Serial.print(sizeof(SensorData));
  Serial.println(" bytes):");
  Serial.print("  nodeId: ");
  Serial.println(sensorData.nodeId);
  Serial.print("  windDirection: ");
  Serial.println(sensorData.windDirection);
  Serial.print("  airSpeed100: ");
  Serial.println(sensorData.airSpeed100);
  Serial.print("  virtualTemp100: ");
  Serial.println(sensorData.virtualTemp100);
  Serial.print("  rssiAbs: ");
  Serial.println(sensorData.rssiAbs);
  // Serial.print("  unixmilli: ");
  // Serial.println(sensorData.unixmilli);
  Serial.print("  Hex: ");
  uint8_t *dataPtr = (uint8_t *)&sensorData;
  for (size_t i = 0; i < sizeof(SensorData); i++) {
    if (dataPtr[i] < 0x10)
      Serial.print("0");
    Serial.print(dataPtr[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // 前回送信からの経過時間を表示
  if (lastSendTime > 0) {
    Serial.print("[ELAPSED] ");
    if (elapsedMs < 10000) { // 10s
      Serial.print(elapsedMs);
      Serial.println(" ms");
    } else {
      Serial.print(elapsedMs / 1000);
      Serial.print(".");
      Serial.print((elapsedMs % 1000) / 100);
      Serial.println(" s");
    }
  }

  delay(200);
  String response = "";
  while (Serial1.available()) {
    char c = Serial1.read();
    response += c;
    Serial.write(c); // デバッグ出力用
  }

  SendResult result = checkSendSuccess(response);

  if (result == SEND_SUCCESS) {
    lastSuccess = true;
    successCount++;
  } else if (result == SEND_SELECT_MODE) {
    // reboot
    Serial.println("[REBOOT] Select Mode detected. Rebooting M5Stack...");
    delay(100); // シリアル出力を確実に送信
    ESP.restart();
  } else {
    // 送信失敗
    lastSuccess = false;
    failCount++;
    lastSendTime = millis(); // 送信時刻を更新
  }

  Serial.print("[STATS] Total: ");
  Serial.print(successCount);
  Serial.print(" success, ");
  Serial.print(failCount);
  Serial.print(" failed");
  if (sendCount > 0) {
    Serial.print(", success rate: ");
    Serial.print((successCount * 100) / sendCount);
    Serial.print("%");
  }
  Serial.println("");

  // ディスプレイ更新
  updateDisplay(sendCount, successCount, failCount, lastSuccess, elapsedMs);

  // Downlinkが来る場合に備えて、常に受信を少し捌く
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c); // デバッグ出力用
  }

  delay(10);
}
