# M-BUS 接続時の干渉分析

## 問題の本質

**「2」コマンドに応答がない**問題は、M-BUS 接続時の干渉が原因と考えられます。
単純な delay では解決せず、ハードウェアレベルの干渉が発生している可能性が高いです。

## 現在の状況

### ピン割り当て

- **ES920LR3（GROVE PORT.A）**: GPIO32（TX）、GPIO33（RX）、Serial1
- **boot_pin**: GPIO22
- **reset_pin**: GPIO19
- **ULSA M5B（M-BUS）**: GPIO13（RX）、GPIO14（TX）、Serial2

### ULSA M5B の実装

```cpp
// ULSAデータ受信用シリアル2の初期化 RXD(GPIO16), TXD(GPIO17)
Serial2.begin(115200, SERIAL_8N1, 13, 14);
```

コメントでは GPIO16/17 と書いてあるが、実際は GPIO13/14 を使用

## 考えられる干渉の原因

### 1. Serial2 の初期化タイミング

- ULSA M5B が Serial2 を使用している
- ES920LR3 の初期化時に Serial2 が既に初期化されている可能性
- Serial2 の初期化が ES920LR3 のシリアル通信に干渉している可能性

### 2. GPIO ピンの競合

- M-BUS が GPIO22 や GPIO19 を使用している可能性
- ULSA M5B が GPIO22 や GPIO19 を制御している可能性
- M-BUS の I2C バスが GPIO22 や GPIO19 を使用している可能性

### 3. 電源供給の競合

- ULSA M5B と ES920LR3 が同時に動作することで電源が不足
- M-BUS 経由の電源供給が ES920LR3 に影響

### 4. シリアル通信の干渉

- Serial1 と Serial2 が同時に動作することで干渉
- ESP32 の UART コントローラーの競合

## 解決策

### 1. Serial2 の初期化を ES920LR3 初期化後に移動

- ES920LR3 の初期化が完了してから Serial2 を初期化
- または、ES920LR3 初期化時に Serial2 を一時的に無効化

### 2. GPIO ピンの状態確認

- boot_pin（GPIO22）と reset_pin（GPIO19）の状態を確認
- M-BUS 接続時と非接続時で比較

### 3. シリアル通信の排他制御

- ES920LR3 初期化中は Serial2 を無効化
- 初期化完了後に Serial2 を再有効化

### 4. M-BUS のピン割り当て確認

- M-BUS が実際に使用している GPIO ピンを確認
- GPIO22 や GPIO19 との競合を確認
