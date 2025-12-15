package main

import (
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"os"
)

// SensorData はM5Core2から送信されるセンサーデータ構造体
// C++の構造体と一致させる必要があります
//
//	struct __attribute__((packed)) SensorData {
//	  uint8_t nodeId;          // Byte 0: 0-10
//	  uint16_t windDirection;  // Byte 1-2: 0-360 (度)
//	  uint16_t airSpeed100;    // Byte 3-4: 0-5000 (値×100)
//	  uint16_t virtualTemp100; // Byte 5-6: 0-5000 (値×100)
//	}
type SensorData struct {
	NodeID         uint8  // Byte 0: 0-10
	WindDirection  uint16 // Byte 1-2: 0-360 (度)
	AirSpeed100    uint16 // Byte 3-4: 0-5000 (値×100)
	VirtualTemp100 uint16 // Byte 5-6: 0-5000 (値×100)
}

// DecodeSensorData base64エンコードされた文字列をデコードしてSensorData構造体に変換
func DecodeSensorData(base64Str string) (*SensorData, error) {
	// Base64デコード
	decoded, err := base64.StdEncoding.DecodeString(base64Str)
	if err != nil {
		return nil, fmt.Errorf("base64 decode failed: %w", err)
	}

	// データサイズの検証（8バイトである必要がある）
	if len(decoded) < 8 {
		return nil, fmt.Errorf("invalid data length: expected at least 8 bytes, got %d bytes", len(decoded))
	}

	// バイナリデータを構造体に変換
	// Arduinoはリトルエンディアンなので、binary.LittleEndianを使用
	data := &SensorData{
		NodeID:         decoded[0],
		WindDirection:  binary.LittleEndian.Uint16(decoded[1:3]),
		AirSpeed100:    binary.LittleEndian.Uint16(decoded[3:5]),
		VirtualTemp100: binary.LittleEndian.Uint16(decoded[5:7]),
	}

	return data, nil
}

// PrintSensorData センサーデータを読みやすい形式で表示
func PrintSensorData(data *SensorData) {
	fmt.Println("=== Sensor Data ===")
	fmt.Printf("Node ID:          %d\n", data.NodeID)
	fmt.Printf("Wind Direction:   %d°\n", data.WindDirection)
	fmt.Printf("Air Speed:        %.2f m/s\n", float64(data.AirSpeed100)/100.0)
	fmt.Printf("Virtual Temp:     %.2f°C\n", float64(data.VirtualTemp100)/100.0)
	fmt.Println("===================")
}

func main() {
	if len(os.Args) < 2 {
		fmt.Println("Usage: go run decode_sensor_data.go <base64_string>")
		fmt.Println("Example: go run decode_sensor_data.go AQAAABsAAAB9AACQAA==")
		os.Exit(1)
	}

	base64Str := os.Args[1]

	// Base64デコードと構造体への変換
	sensorData, err := DecodeSensorData(base64Str)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}

	// 結果を表示
	PrintSensorData(sensorData)
}



