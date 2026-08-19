#pragma once

// Log macros — chỉ compile ra code trong DEBUG build.
// Release build → không sinh code (Serial.print* biến mất hoàn toàn),
// tiết kiệm flash + tránh lộ log ở production.
//
// Cách dùng:
//   LOG_PRINTLN("hello");
//   LOG_PRINTF("val=%d\n", n);
//   LOG_FLUSH();
//
// Serial.begin(...) trong setup vẫn cần giữ (không dùng macro) — Serial
// vẫn có thể dùng cho input (Serial.available/read) ở loop().

#ifdef DEBUG
  #include <Arduino.h>
  #define LOG_PRINT(...)    Serial.print(__VA_ARGS__)
  #define LOG_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define LOG_PRINTF(...)   Serial.printf(__VA_ARGS__)
  #define LOG_FLUSH()       Serial.flush()
#else
  #define LOG_PRINT(...)    ((void)0)
  #define LOG_PRINTLN(...)  ((void)0)
  #define LOG_PRINTF(...)   ((void)0)
  #define LOG_FLUSH()       ((void)0)
#endif
