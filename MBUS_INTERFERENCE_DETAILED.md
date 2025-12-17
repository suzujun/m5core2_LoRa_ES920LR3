# M-BUS 接続時の詳細な干渉分析

## 問題の状況

### M-BUS 未接続時

- ES920LR3 へのコマンドはリトライなしで正常に動作
- JOIN も正常に完了（300 秒後に JOIN 成功）

### M-BUS 接続時

- 1 コマンドにつき 3-4 回のリトライが必要
- NG 102 エラーが頻発
- 最終的に JOIN ができない

## ULSA M5B の実装分析

### Serial2 の初期化タイミング

```cpp
// ULSAデータ受信用シリアル2の初期化
Serial2.begin(115200, SERIAL_8N1, 13, 14);
while (!Serial2)
  delay(10);
```

### マルチタスク構成

- `recvUART`タスク（優先度 3、CPU Core 1）: Serial2 から ULSA M5B のデータを受信
- `connectCloud`タスク（優先度 2、CPU Core 0）: WiFi/AWS IoT 接続
- `drawProcess`タスク（優先度 1、CPU Core 0）: LCD 描画

### 重要な発見

1. **Serial2 は常に動作している**: recvUART タスクが常に Serial2 からデータを受信している
2. **マルチタスク環境**: FreeRTOS のマルチタスク環境で動作している
3. **ミューテックス使用**: recvUART タスクはミューテックスを使用して排他制御している

## 考えられる干渉の原因

### 1. Serial2 の受信処理による干渉

- recvUART タスクが Serial2 からデータを受信している間、ESP32 の UART コントローラーが使用されている
- Serial1 と Serial2 が同じ UART コントローラーを使用している可能性
- Serial2 の受信処理が Serial1 の送受信に影響を与えている可能性

### 2. GPIO ピンの競合

- M-BUS が GPIO22（boot_pin）や GPIO19（reset_pin）を使用している可能性
- ULSA M5B が GPIO22 や GPIO19 を制御している可能性
- M-BUS の I2C バスが GPIO22 や GPIO19 を使用している可能性

### 3. 電源供給の競合

- ULSA M5B と ES920LR3 が同時に動作することで電源が不足
- M-BUS 経由の電源供給が ES920LR3 に影響
- 電源電圧の変動が ES920LR3 の動作に影響

### 4. CPU リソースの競合

- recvUART タスクが CPU Core 1 で動作している
- ES920LR3 の初期化も CPU Core 1 で実行されている可能性
- CPU リソースの競合が ES920LR3 の初期化に影響

### 5. シリアル通信のバッファ競合

- Serial2 の受信バッファが満杯になっている場合、ESP32 の UART コントローラーに負荷がかかる
- Serial1 と Serial2 のバッファが競合している可能性

## 解決策

### 1. Serial2 の受信を一時停止

- ES920LR3 初期化中は、recvUART タスクを一時停止する
- または、Serial2 の受信バッファをクリアする

### 2. GPIO ピンの状態確認

- M-BUS 接続時と非接続時で GPIO22 と GPIO19 の状態を比較
- GPIO ピンの競合を確認

### 3. 電源供給の確認

- M-BUS 接続時と非接続時で電源電圧を測定
- 電源供給が十分か確認

### 4. CPU リソースの最適化

- ES920LR3 初期化を CPU Core 0 で実行する
- または、recvUART タスクの優先度を下げる

### 5. シリアル通信のバッファ管理

- Serial2 の受信バッファを定期的にクリアする
- Serial1 と Serial2 のバッファサイズを調整する
