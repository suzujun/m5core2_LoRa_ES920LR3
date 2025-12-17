#pragma once

// LoRaWAN認証情報のテンプレートファイル
// このファイルをコピーして secrets.h を作成し、実際の値を設定してください
// secrets.h は .gitignore に追加されているため、誤ってコミットされることはありません

// DevEUI: 16進数16文字（メーカー提供）
#define DEV_EUI "YOUR_DEV_EUI_HERE"

// AppEUI: 16進数16文字（自分で決めた JoinEUI/AppEUI）
#define APP_EUI "YOUR_APP_EUI_HERE"

// AppKey: 16進数32文字（自分で生成した 128bit鍵）
#define APP_KEY "YOUR_APP_KEY_HERE"
