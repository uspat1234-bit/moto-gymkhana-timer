#pragma once
#include <FS.h>

// ESP8266AudioのAudioFileSourceFS/AudioFileSourceSPIFFSが
// コンパイルを通すためだけのダミー宣言。
// 実体はsrc/main.cpp側でLittleFSを使って定義する。
extern fs::FS SPIFFS;