// MGTS_NFC.h
#ifndef MGTS_NFC_H
#define MGTS_NFC_H

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>

extern PN532 nfc;

// NTAGまたはClassicからIDを直接読み取る関数
String readTagID() {
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  String readID = "";

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500)) {
    if (uidLength == 7) { // NTAG
      uint8_t buffer[32];
      if (nfc.mifareultralight_ReadPage(4, buffer)) {
        for (int i = 0; i < 4; i++) readID += (char)buffer[i];
      }
    } else if (uidLength == 4) { // Classic
      uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya)) {
        uint8_t buffer[16];
        if (nfc.mifareclassic_ReadDataBlock(4, buffer)) {
          for (int i = 0; i < 4; i++) readID += (char)buffer[i];
        }
      }
    }
  }
  return readID;
}

#endif
