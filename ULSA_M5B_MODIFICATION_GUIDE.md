# ULSA M5B 側の修正ガイド

## 概要

ES920LR3 のコマンド応答遅延問題を根本的に解決するため、ULSA M5B 側の`recvUART`タスクを修正し、ES920LR3 のコマンド送信中は一時停止する機能を追加します。

## 修正内容

### 1. グローバル変数の追加

`ULSA_M5B_Sample_100.ino`に以下の変数を追加：

```cpp
// ES920LR3コマンド送信中のSerial2受信一時停止フラグ
volatile bool pauseSerial2ForLoRa = false;

// セマフォまたはミューテックスを使用する場合（推奨）
SemaphoreHandle_t xLoRaCommandMutex = NULL;
```

### 2. setup()関数の修正

`setup()`関数内で、セマフォを初期化：

```cpp
void setup() {
  // ... 既存のコード ...

  // ES920LR3コマンド送信用のミューテックスを作成
  xLoRaCommandMutex = xSemaphoreCreateMutex();
  if (xLoRaCommandMutex == NULL) {
    Serial.println("Failed to create LoRa command mutex.");
    // エラーハンドリング
  }

  // ... 既存のコード ...
}
```

### 3. recvUART()関数の修正

`recvUART()`関数を以下のように修正：

```cpp
// ULSA M5BからのUART送信データをポーリングで受信する
void recvUART(void *pvParameters) {
  while (1) {
    // ES920LR3のコマンド送信中は一時停止
    // 方法1: フラグを使用（簡単だが、タイミングがずれる可能性）
    if (pauseSerial2ForLoRa) {
      delay(10); // 短い待機時間
      continue;
    }

    // 方法2: セマフォを使用（推奨）
    // ES920LR3がコマンド送信中の場合、セマフォが取得できないため待機
    if (xLoRaCommandMutex != NULL) {
      // ノンブロッキングでセマフォを取得
      if (xSemaphoreTake(xLoRaCommandMutex, 0) == pdTRUE) {
        // セマフォを取得できた場合のみ処理を実行
        xSemaphoreGive(xLoRaCommandMutex);
      } else {
        // ES920LR3がコマンド送信中の場合、待機
        delay(10);
        continue;
      }
    }

    // ミューテックスを取得（ロック）
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
      // UARTポートからULSAの計測データを受信する
      recvData();

      // ミューテックスを解放（アンロック）
      xSemaphoreGive(xMutex);
    }
    // 他タスクの動作のためCPU時間を確保する
    delay(1);
  }
}
```

### 4. ES920LR3 側との連携方法

#### 方法 1: 共有メモリを使用（簡単だが、タイミングがずれる可能性）

**ULSA M5B 側**:

```cpp
// グローバル変数として宣言（volatile修飾子を付ける）
extern volatile bool pauseSerial2ForLoRa;
```

**ES920LR3 側**:

```cpp
// コマンド送信前にフラグを設定
pauseSerial2ForLoRa = true;
sendCommand("v", 2000);
pauseSerial2ForLoRa = false;
```

#### 方法 2: FreeRTOS のセマフォを使用（推奨）

**ULSA M5B 側**:

```cpp
// グローバル変数として宣言
extern SemaphoreHandle_t xLoRaCommandMutex;
```

**ES920LR3 側**:

```cpp
// コマンド送信前にセマフォを取得
if (xLoRaCommandMutex != NULL) {
  xSemaphoreTake(xLoRaCommandMutex, portMAX_DELAY);
}

sendCommand("v", 2000);

// コマンド送信後にセマフォを解放
if (xLoRaCommandMutex != NULL) {
  xSemaphoreGive(xLoRaCommandMutex);
}
```

#### 方法 3: FreeRTOS のイベントグループを使用（最も柔軟）

**ULSA M5B 側**:

```cpp
// イベントグループを作成
EventGroupHandle_t xLoRaEventGroup = NULL;

void setup() {
  // ... 既存のコード ...

  // イベントグループを作成
  xLoRaEventGroup = xEventGroupCreate();
  if (xLoRaEventGroup == NULL) {
    Serial.println("Failed to create LoRa event group.");
  }

  // ... 既存のコード ...
}

void recvUART(void *pvParameters) {
  while (1) {
    // ES920LR3のコマンド送信中は一時停止
    if (xLoRaEventGroup != NULL) {
      // ES920LR3コマンド送信ビットが設定されている場合、待機
      EventBits_t bits = xEventGroupGetBits(xLoRaEventGroup);
      if (bits & 0x01) { // ビット0がES920LR3コマンド送信中を示す
        delay(10);
        continue;
      }
    }

    // ... 既存のコード ...
  }
}
```

**ES920LR3 側**:

```cpp
// コマンド送信前にイベントビットを設定
if (xLoRaEventGroup != NULL) {
  xEventGroupSetBits(xLoRaEventGroup, 0x01);
}

sendCommand("v", 2000);

// コマンド送信後にイベントビットをクリア
if (xLoRaEventGroup != NULL) {
  xEventGroupClearBits(xLoRaEventGroup, 0x01);
}
```

## 実装の優先順位

### 優先度 1: セマフォを使用した方法（方法 2）

- **メリット**: 確実に排他制御ができる
- **デメリット**: ULSA M5B と ES920LR3 の両方のコードを修正する必要がある
- **実装難易度**: 中

### 優先度 2: フラグを使用した方法（方法 1）

- **メリット**: 実装が簡単
- **デメリット**: タイミングがずれる可能性がある
- **実装難易度**: 低

### 優先度 3: イベントグループを使用した方法（方法 3）

- **メリット**: 最も柔軟で拡張性が高い
- **デメリット**: 実装が複雑
- **実装難易度**: 高

## 注意事項

1. **volatile 修飾子**: フラグ変数には必ず`volatile`修飾子を付ける
2. **ミューテックスのデッドロック**: セマフォを使用する場合、デッドロックに注意
3. **タイムアウト**: セマフォの取得にはタイムアウトを設定する
4. **パフォーマンス**: 一時停止時間が長すぎると、ULSA M5B のデータ受信に影響する可能性

## テスト方法

1. **単体テスト**: ULSA M5B 側の修正を単独でテスト
2. **統合テスト**: ES920LR3 と ULSA M5B を同時に動作させてテスト
3. **負荷テスト**: 長時間動作テスト（24 時間以上）

## 期待される効果

- **コマンド応答時間**: 数 ms（M-BUS 未接続時と同等）
- **NG 102 エラー**: ほぼゼロ
- **リトライ回数**: 0 回（初回で成功）
- **ULSA M5B のデータ受信**: 影響なし（一時停止時間が短いため）
