/*
 * ESP8266 ระบบควบคุมไฟอัตโนมัติ
 * คุณสมบัติ:
 * - ควบคุมรีเลย์ 2 ตัวผ่าน Blynk
 * - มีโหมดการทำงาน 3 โหมด: ควบคุมด้วยตนเอง, อัตโนมัติตามแสง, ตั้งเวลา
 * - การทำงานแบบอัตโนมัติตามแสงมีการดีเลย์ก่อนเปิด-ปิดไฟเพื่อลดการกระพริบ
 */

//============================= การตั้งค่า Blynk =============================
#define BLYNK_TEMPLATE_ID "TMPL6dWoSA8qJ"
#define BLYNK_TEMPLATE_NAME "ESP8266"
#define BLYNK_AUTH_TOKEN "MW8CFjWSPrXIIFEVW_B6_V-2XFHVIrhR"
#define BLYNK_FIRMWARE_VERSION "0.2.0"
#define BLYNK_PRINT Serial

//============================= Libraries =============================
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <RTClib.h>

//============================= การกำหนดขา GPIO =============================
#define LDR_PIN A0      // ขา Analog สำหรับเซ็นเซอร์แสง LDR
#define RELAY1_PIN D5   // ขาควบคุมรีเลย์ 1
#define RELAY2_PIN D6   // ขาควบคุมรีเลย์ 2

//============================= สถานะของรีเลย์ =============================
// โหมดการทำงาน
enum ControlMode {
  MANUAL,     // ควบคุมด้วยตนเอง
  AUTO_LIGHT, // อัตโนมัติตามแสง
  TIMER       // ตั้งเวลา
};

// ตัวแปรเก็บสถานะของรีเลย์
bool relay1State = false;          // สถานะของรีเลย์ 1 (false = ปิด, true = เปิด)
bool relay2State = false;          // สถานะของรีเลย์ 2
ControlMode relay1Mode = MANUAL;   // โหมดการทำงานของรีเลย์ 1
ControlMode relay2Mode = MANUAL;   // โหมดการทำงานของรีเลย์ 2

//============================= การตั้งค่า WiFi =============================
const char* ssid = "Arm_llajisn";    // ชื่อ WiFi
const char* password = "11111111";   // รหัสผ่าน WiFi

//============================= ตัวแปรสำหรับการดีเลย์ในโหมด AUTO_LIGHT =============================
// ตัวแปรเกี่ยวกับการดีเลย์เปิดไฟ
unsigned long lightOffTime1 = 0;     // เวลาที่เริ่มนับสำหรับการเปิดไฟรีเลย์ 1
unsigned long lightOffTime2 = 0;     // เวลาที่เริ่มนับสำหรับการเปิดไฟรีเลย์ 2
bool waitingToTurnOn1 = false;       // กำลังรอเปิดไฟรีเลย์ 1
bool waitingToTurnOn2 = false;       // กำลังรอเปิดไฟรีเลย์ 2

// ตัวแปรเกี่ยวกับการดีเลย์ปิดไฟ
unsigned long LIGHT_ON_DELAY = 10000;  // ดีเลย์เปิดไฟ 10 วินาที (มิลลิวินาที)
unsigned long LIGHT_OFF_DELAY = 5000;  // ดีเลย์ปิดไฟ 5 วินาที (มิลลิวินาที)
unsigned long lightOnTime1 = 0;        // เวลาที่เริ่มนับสำหรับการปิดไฟรีเลย์ 1
unsigned long lightOnTime2 = 0;        // เวลาที่เริ่มนับสำหรับการปิดไฟรีเลย์ 2
bool waitingToTurnOff1 = false;        // กำลังรอปิดไฟรีเลย์ 1
bool waitingToTurnOff2 = false;        // กำลังรอปิดไฟรีเลย์ 2

//============================= ตัวแปรสำหรับการตั้งเวลา =============================
long timer_start_set[2] = {0xFFFF, 0xFFFF};  // เวลาเริ่มต้นสำหรับรีเลย์ 1 และ 2
long timer_stop_set[2] = {0xFFFF, 0xFFFF};   // เวลาสิ้นสุดสำหรับรีเลย์ 1 และ 2
unsigned char weekday_set[2] = {0, 0};       // วันในสัปดาห์สำหรับรีเลย์ 1 และ 2
bool led_timer_on_set[2] = {false, false};   // สถานะการใช้งานตัวตั้งเวลา

//============================= ตัวแปรสำหรับ LDR =============================
int LDR_THRESHOLD = 600;  // ค่าขีดแบ่งแสง - มากกว่านี้ถือว่ามืด (ค่าเริ่มต้น)

//============================= อ็อบเจกต์ต่างๆ =============================
RTC_DS3231 rtc;      // นาฬิกา RTC
BlynkTimer timer;    // Timer สำหรับ Blynk

//============================= ฟังก์ชันควบคุมรีเลย์ =============================
// ฟังก์ชันควบคุมรีเลย์ 1
void toggleRelay1(bool newState) {
  // ตรวจสอบว่ามีการเปลี่ยนสถานะจริงหรือไม่
  if (relay1State != newState) {
    relay1State = newState;
    digitalWrite(RELAY1_PIN, relay1State ? LOW : HIGH);  // รีเลย์ทำงานแบบ Active LOW
    
    // อัพเดตสถานะใน Blynk
    Blynk.virtualWrite(V8, relay1State);
    Blynk.virtualWrite(V0, relay1State);

    Serial.print("Relay 1 State: ");
    Serial.println(relay1State ? "ON" : "OFF");
  }
}

// ฟังก์ชันควบคุมรีเลย์ 2
void toggleRelay2(bool newState) {
  // ตรวจสอบว่ามีการเปลี่ยนสถานะจริงหรือไม่
  if (relay2State != newState) {
    relay2State = newState;
    digitalWrite(RELAY2_PIN, relay2State ? LOW : HIGH);  // รีเลย์ทำงานแบบ Active LOW
    
    // อัพเดตสถานะใน Blynk
    Blynk.virtualWrite(V9, relay2State);
    Blynk.virtualWrite(V1, relay2State);
    
    Serial.print("Relay 2 State: ");
    Serial.println(relay2State ? "ON" : "OFF");
  }
}

//============================= ฟังก์ชันจัดการตามโหมดการทำงาน =============================
// จัดการรีเลย์ตามโหมดตั้งเวลา
void manageTimerRelay() {
  DateTime now = rtc.now();
  
  // แปลงเวลาปัจจุบันเป็นวินาที
  long current_time_sec = (now.hour() * 3600) + (now.minute() * 60) + now.second();
  
  // ตรวจสอบทั้งรีเลย์ 1 และ 2
  for (int i = 0; i < 2; i++) {
    if (timer_start_set[i] != 0xFFFF && timer_stop_set[i] != 0xFFFF) {
      bool time_set_overflow = (timer_stop_set[i] < timer_start_set[i]);

      bool timeCondition = false;
      if (time_set_overflow) {
        // กรณีข้ามวัน (เช่น 22:00 - 06:00)
        timeCondition = (current_time_sec >= timer_start_set[i]) || 
                        (current_time_sec < timer_stop_set[i]);
      } else {
        // กรณีปกติ (เช่น 08:00 - 17:00)
        timeCondition = (current_time_sec >= timer_start_set[i]) && 
                        (current_time_sec < timer_stop_set[i]);
      }

      // จัดการรีเลย์ 1
      if (i == 0 && relay1Mode == TIMER) {
        // กำหนดสถานะใหม่ตามเงื่อนไขเวลา
        bool newRelayState = timeCondition;
        
        // ถ้าสถานะเปลี่ยนไป ให้อัพเดต
        if (relay1State != newRelayState) {
          toggleRelay1(newRelayState);
        }
        
        // รีเซ็ตสถานะดีเลย์เมื่อเข้าโหมดตั้งเวลา
        waitingToTurnOn1 = false;
        waitingToTurnOff1 = false;
      }

      // จัดการรีเลย์ 2
      if (i == 1 && relay2Mode == TIMER) {
        // กำหนดสถานะใหม่ตามเงื่อนไขเวลา
        bool newRelayState = timeCondition;
        
        // ถ้าสถานะเปลี่ยนไป ให้อัพเดต
        if (relay2State != newRelayState) {
          toggleRelay2(newRelayState);
        }
        
        // รีเซ็ตสถานะดีเลย์เมื่อเข้าโหมดตั้งเวลา
        waitingToTurnOn2 = false;
        waitingToTurnOff2 = false;
      }
    }
  }
}

// จัดการรีเลย์ตามแสง (Auto Light Mode)
void controlRelayByLight() {
  // อ่านค่าแสงโดยตรงจาก LDR
  int ldrValue = analogRead(LDR_PIN);
  
  // ส่งค่าไปยัง Blynk เพื่อแสดงผล
  Blynk.virtualWrite(V10, ldrValue);
  
  unsigned long currentMillis = millis();
  
  //---------------------- ควบคุมรีเลย์ 1 โหมด AUTO_LIGHT ----------------------
  if (relay1Mode == AUTO_LIGHT) {
    //--- เมื่อแสงน้อยลง (มืด) - เริ่มนับเวลารอเปิดไฟ ---
    if (ldrValue > LDR_THRESHOLD && !waitingToTurnOn1 && !relay1State && !waitingToTurnOff1) {
      lightOffTime1 = currentMillis;
      waitingToTurnOn1 = true;
      
      Serial.println("------------------------");
      Serial.println("Relay 1 Auto Light Mode");
      Serial.print("Light Level Low (");
      Serial.print(ldrValue);
      Serial.print(" > ");
      Serial.print(LDR_THRESHOLD);
      Serial.print(") - Starting ");
      Serial.print(LIGHT_ON_DELAY / 1000);
      Serial.println("s delay before turning ON");
    }
    
    //--- ถ้ากำลังรอเปิดไฟ และครบเวลาแล้ว ---
    if (waitingToTurnOn1 && (currentMillis - lightOffTime1 >= LIGHT_ON_DELAY)) {
      // ตรวจสอบอีกครั้งว่ายังมืดอยู่ก่อนเปิดไฟจริง
      if (ldrValue > LDR_THRESHOLD) {
        toggleRelay1(true);
      }
      waitingToTurnOn1 = false;
    }
    
    //--- เมื่อแสงมากขึ้น (สว่าง) - เริ่มนับเวลารอปิดไฟ ---
    if (ldrValue <= LDR_THRESHOLD && !waitingToTurnOff1 && relay1State && !waitingToTurnOn1) {
      lightOnTime1 = currentMillis;
      waitingToTurnOff1 = true;
      
      Serial.println("------------------------");
      Serial.println("Relay 1 Auto Light Mode");
      Serial.print("Light Level High (");
      Serial.print(ldrValue);
      Serial.print(" <= ");
      Serial.print(LDR_THRESHOLD);
      Serial.print(") - Starting ");
      Serial.print(LIGHT_OFF_DELAY / 1000);
      Serial.println("s delay before turning OFF");
    }
    
    //--- ถ้ากำลังรอปิดไฟ และครบเวลาแล้ว ---
    if (waitingToTurnOff1 && (currentMillis - lightOnTime1 >= LIGHT_OFF_DELAY)) {
      // ตรวจสอบอีกครั้งว่ายังสว่างอยู่ก่อนปิดไฟจริง
      if (ldrValue <= LDR_THRESHOLD) {
        toggleRelay1(false);
      }
      waitingToTurnOff1 = false;
    }
    
    //--- ยกเลิกการรอในกรณีที่แสงเปลี่ยนกลับระหว่างรอ ---
    // ถ้าในระหว่างที่รอปิดไฟ แต่แสงกลับน้อยลงอีก (กลับมืด) ยกเลิกการรอ
    if (waitingToTurnOff1 && ldrValue > LDR_THRESHOLD) {
      waitingToTurnOff1 = false;
      
      Serial.println("------------------------");
      Serial.println("Relay 1 Auto Light Mode");
      Serial.println("Light Level returned to low - Canceling OFF delay");
    }
    
    // ถ้าในระหว่างที่รอเปิดไฟ แต่แสงกลับมากขึ้น (กลับสว่าง) ยกเลิกการรอ
    if (waitingToTurnOn1 && ldrValue <= LDR_THRESHOLD) {
      waitingToTurnOn1 = false;
      
      Serial.println("------------------------");
      Serial.println("Relay 1 Auto Light Mode");
      Serial.println("Light Level returned to high - Canceling ON delay");
    }
  }
  
  //---------------------- ควบคุมรีเลย์ 2 โหมด AUTO_LIGHT ----------------------
  if (relay2Mode == AUTO_LIGHT) {
    //--- เมื่อแสงน้อยลง (มืด) - เริ่มนับเวลารอเปิดไฟ ---
    if (ldrValue > LDR_THRESHOLD && !waitingToTurnOn2 && !relay2State && !waitingToTurnOff2) {
      lightOffTime2 = currentMillis;
      waitingToTurnOn2 = true;
      
      Serial.println("------------------------");
      Serial.println("Relay 2 Auto Light Mode");
      Serial.print("Light Level Low (");
      Serial.print(ldrValue);
      Serial.print(" > ");
      Serial.print(LDR_THRESHOLD);
      Serial.print(") - Starting ");
      Serial.print(LIGHT_ON_DELAY / 1000);
      Serial.println("s delay before turning ON");
    }
    
    //--- ถ้ากำลังรอเปิดไฟ และครบเวลาแล้ว ---
    if (waitingToTurnOn2 && (currentMillis - lightOffTime2 >= LIGHT_ON_DELAY)) {
      // ตรวจสอบอีกครั้งว่ายังมืดอยู่ก่อนเปิดไฟจริง
      if (ldrValue > LDR_THRESHOLD) {
        toggleRelay2(true);
      }
      waitingToTurnOn2 = false;
    }
    
    //--- เมื่อแสงมากขึ้น (สว่าง) - เริ่มนับเวลารอปิดไฟ ---
    if (ldrValue <= LDR_THRESHOLD && !waitingToTurnOff2 && relay2State && !waitingToTurnOn2) {
      lightOnTime2 = currentMillis;
      waitingToTurnOff2 = true;
      
      Serial.println("------------------------");
      Serial.println("Relay 2 Auto Light Mode");
      Serial.print("Light Level High (");
      Serial.print(ldrValue);
      Serial.print(" <= ");
      Serial.print(LDR_THRESHOLD);
      Serial.print(") - Starting ");
      Serial.print(LIGHT_OFF_DELAY / 1000);
      Serial.println("s delay before turning OFF");
    }
    
    //--- ถ้ากำลังรอปิดไฟ และครบเวลาแล้ว ---
    if (waitingToTurnOff2 && (currentMillis - lightOnTime2 >= LIGHT_OFF_DELAY)) {
      // ตรวจสอบอีกครั้งว่ายังสว่างอยู่ก่อนปิดไฟจริง
      if (ldrValue <= LDR_THRESHOLD) {
        toggleRelay2(false);
      }
      waitingToTurnOff2 = false;
    }
    
    //--- ยกเลิกการรอในกรณีที่แสงเปลี่ยนกลับระหว่างรอ ---
    // ถ้าในระหว่างที่รอปิดไฟ แต่แสงกลับน้อยลงอีก (กลับมืด) ยกเลิกการรอ
    if (waitingToTurnOff2 && ldrValue > LDR_THRESHOLD) {
      waitingToTurnOff2 = false;
      
      Serial.println("------------------------");
      Serial.println("Relay 2 Auto Light Mode");
      Serial.println("Light Level returned to low - Canceling OFF delay");
    }
    
    // ถ้าในระหว่างที่รอเปิดไฟ แต่แสงกลับมากขึ้น (กลับสว่าง) ยกเลิกการรอ
    if (waitingToTurnOn2 && ldrValue <= LDR_THRESHOLD) {
      waitingToTurnOn2 = false;
      
      Serial.println("------------------------");
      Serial.println("Relay 2 Auto Light Mode");
      Serial.println("Light Level returned to high - Canceling ON delay");
    }
  }
}

//============================= ฟังก์ชันตรวจสอบเวลา =============================
void checkTime() {
  DateTime now = rtc.now();
  Serial.print("Current Time: ");
  Serial.print(now.hour());
  Serial.print(":");
  Serial.print(now.minute());
  Serial.print(":");
  Serial.println(now.second());
}

//============================= การจัดการวิดเจ็ต Blynk =============================
// วิดเจ็ตปรับค่า LDR threshold (V11)
BLYNK_WRITE(V11) {
  LDR_THRESHOLD = param.asInt();
  
  Serial.println("------------------------");
  Serial.print("LDR Threshold changed to: ");
  Serial.println(LDR_THRESHOLD);
}

// วิดเจ็ตสำหรับดีเลย์เปิดไฟ (V12)
BLYNK_WRITE(V12) {
  LIGHT_ON_DELAY = param.asInt() * 1000;  // แปลงเป็นมิลลิวินาทีจากวินาที
  
  Serial.println("------------------------");
  Serial.print("Light ON delay changed to: ");
  Serial.print(LIGHT_ON_DELAY / 1000);
  Serial.println(" seconds");
}

// วิดเจ็ตสำหรับดีเลย์ปิดไฟ (V13)
BLYNK_WRITE(V13) {
  LIGHT_OFF_DELAY = param.asInt() * 1000;  // แปลงเป็นมิลลิวินาทีจากวินาที
  
  Serial.println("------------------------");
  Serial.print("Light OFF delay changed to: ");
  Serial.print(LIGHT_OFF_DELAY / 1000);
  Serial.println(" seconds");
}

// ปุ่มควบคุมรีเลย์ 1 (V0)
BLYNK_WRITE(V0) {
  int val = param.asInt();
  
  Serial.println("------------------------");
  Serial.println("V0 Button Pressed (Relay 1)");
  Serial.print("Value: ");
  Serial.println(val);
  
  // ตรวจสอบว่าอยู่ในโหมด MANUAL เท่านั้น
  if (relay1Mode == MANUAL) {
    relay1State = val;
    digitalWrite(RELAY1_PIN, relay1State ? LOW : HIGH);
    
    // อัพเดตสถานะใน Blynk
    Blynk.virtualWrite(V8, relay1State);

    Serial.print("Relay 1 State: ");
    Serial.println(relay1State ? "ON" : "OFF");
  } else {
    // ถ้าไม่ได้อยู่ในโหมด MANUAL แสดงข้อความแจ้งเตือน
    Serial.println("Cannot control relay directly - Not in MANUAL mode");
    // อัพเดตปุ่มให้ตรงกับสถานะปัจจุบัน
    Blynk.virtualWrite(V0, relay1State);
  }
}

// ปุ่มควบคุมรีเลย์ 2 (V1)
BLYNK_WRITE(V1) {
  int val = param.asInt();
  
  Serial.println("------------------------");
  Serial.println("V1 Button Pressed (Relay 2)");
  Serial.print("Value: ");
  Serial.println(val);
  
  // ตรวจสอบว่าอยู่ในโหมด MANUAL เท่านั้น
  if (relay2Mode == MANUAL) {
    relay2State = val;
    digitalWrite(RELAY2_PIN, relay2State ? LOW : HIGH);
    
    // อัพเดตสถานะใน Blynk
    Blynk.virtualWrite(V9, relay2State);
    
    Serial.print("สถานะรีเลย์ 2: ");
    Serial.println(relay2State ? "เปิด" : "ปิด");
  } else {
    // ถ้าไม่ได้อยู่ในโหมด MANUAL แสดงข้อความแจ้งเตือน
    Serial.println("ไม่สามารถควบคุมรีเลย์โดยตรงได้ - ไม่ได้อยู่ในโหมด MANUAL");
    // อัพเดตปุ่มให้ตรงกับสถานะปัจจุบัน
    Blynk.virtualWrite(V1, relay2State);
  }
}

// ปุ่มเลือกโหมดรีเลย์ 1 (V2)
BLYNK_WRITE(V2) {  
  int modeValue = param.asInt();
  relay1Mode = static_cast<ControlMode>(modeValue);
  
  Serial.println("------------------------");
  Serial.println("เปลี่ยนโหมดรีเลย์ 1");
  Serial.print("โหมด: ");
  switch(relay1Mode) {
    case MANUAL: 
      Serial.println("ควบคุมด้วยตนเอง");
      Blynk.virtualWrite(V18, "MANUAL");
      break;
    case AUTO_LIGHT: 
      Serial.println("อัตโนมัติตามแสง");
      Blynk.virtualWrite(V18, "AUTO LIGHT");
      break;
    case TIMER: 
      Serial.println("ตั้งเวลา");
      Blynk.virtualWrite(V18, "TIMER");
      break;
  }
  
  // รีเซ็ตการดีเลย์เมื่อเปลี่ยนโหมด
  waitingToTurnOn1 = false;
  waitingToTurnOff1 = false;
  
  // อัพเดตสถานะปุ่มให้ตรงกับสถานะปัจจุบัน
  Blynk.virtualWrite(V0, relay1State);
}

// ปุ่มเลือกโหมดรีเลย์ 2 (V3)
BLYNK_WRITE(V3) {  
  int modeValue = param.asInt();
  relay2Mode = static_cast<ControlMode>(modeValue);
  
  Serial.println("------------------------");
  Serial.println("เปลี่ยนโหมดรีเลย์ 2");
  Serial.print("โหมด: ");
  switch(relay2Mode) {
    case MANUAL: 
      Serial.println("ควบคุมด้วยตนเอง");
      Blynk.virtualWrite(V19, "MANUAL");
      break;
    case AUTO_LIGHT: 
      Serial.println("อัตโนมัติตามแสง");
      Blynk.virtualWrite(V19, "AUTO LIGHT");
      break;
    case TIMER: 
      Serial.println("ตั้งเวลา");
      Blynk.virtualWrite(V19, "TIMER");
      break;
  }
  
  // รีเซ็ตการดีเลย์เมื่อเปลี่ยนโหมด
  waitingToTurnOn2 = false;
  waitingToTurnOff2 = false;
  
  // อัพเดตสถานะปุ่มให้ตรงกับสถานะปัจจุบัน
  Blynk.virtualWrite(V1, relay2State);
}

// ตัวตั้งเวลาสำหรับรีเลย์ 1 (V4)
BLYNK_WRITE(V4) {
  unsigned char week_day = 0;
  TimeInputParam t(param);

  Serial.println("------------------------");
  Serial.println("อัพเดตการตั้งเวลารีเลย์ 1");

  if (t.hasStartTime() && t.hasStopTime()) {
    timer_start_set[0] = (t.getStartHour() * 60 * 60) + (t.getStartMinute() * 60) + t.getStartSecond();
    timer_stop_set[0] = (t.getStopHour() * 60 * 60) + (t.getStopMinute() * 60) + t.getStopSecond();
    
    Serial.print("เวลาเริ่มต้น: ");
    Serial.print(t.getStartHour());
    Serial.print(":");
    Serial.print(t.getStartMinute());
    Serial.print(":");
    Serial.println(t.getStartSecond());
    
    Serial.print("เวลาสิ้นสุด: ");
    Serial.print(t.getStopHour());
    Serial.print(":");
    Serial.print(t.getStopMinute());
    Serial.print(":");
    Serial.println(t.getStopSecond());
    
    Serial.println("วันที่เลือก:");
    for (int i = 1; i <= 7; i++) {
      if (t.isWeekdaySelected(i)) {
        week_day |= (0x01 << (i-1));
        Serial.print("วันที่ ");
        Serial.print(i);
        Serial.println(" ถูกเลือก");
      }
    }

    weekday_set[0] = week_day;
  } else {
    timer_start_set[0] = 0xFFFF;
    timer_stop_set[0] = 0xFFFF;
    Serial.println("ล้างการตั้งเวลารีเลย์ 1");
  }
}

// ตัวตั้งเวลาสำหรับรีเลย์ 2 (V5)
BLYNK_WRITE(V5) {
  unsigned char week_day = 0;
  TimeInputParam t(param);

  Serial.println("------------------------");
  Serial.println("อัพเดตการตั้งเวลารีเลย์ 2");

  if (t.hasStartTime() && t.hasStopTime()) {
    timer_start_set[1] = (t.getStartHour() * 60 * 60) + (t.getStartMinute() * 60) + t.getStartSecond();
    timer_stop_set[1] = (t.getStopHour() * 60 * 60) + (t.getStopMinute() * 60) + t.getStopSecond();
    
    Serial.print("เวลาเริ่มต้น: ");
    Serial.print(t.getStartHour());
    Serial.print(":");
    Serial.print(t.getStartMinute());
    Serial.print(":");
    Serial.println(t.getStartSecond());
    
    Serial.print("เวลาสิ้นสุด: ");
    Serial.print(t.getStopHour());
    Serial.print(":");
    Serial.print(t.getStopMinute());
    Serial.print(":");
    Serial.println(t.getStopSecond());
    
    Serial.println("วันที่เลือก:");
    for (int i = 1; i <= 7; i++) {
      if (t.isWeekdaySelected(i)) {
        week_day |= (0x01 << (i-1));
        Serial.print("วันที่ ");
        Serial.print(i);
        Serial.println(" ถูกเลือก");
      }
    }

    weekday_set[1] = week_day;
  } else {
    timer_start_set[1] = 0xFFFF;
    timer_stop_set[1] = 0xFFFF;
    Serial.println("ล้างการตั้งเวลารีเลย์ 2");
  }
}

//============================= ฟังก์ชัน setup และ loop =============================
void setup() {
  Serial.begin(115200);
  // กำหนดให้แสดงผลภาษาไทยได้ (UTF-8)
  // Serial.setRxBufferSize(256);
  
  Serial.println();
  Serial.println("========================");
  Serial.println("เริ่มต้นระบบควบคุมไฟอัตโนมัติ");
  Serial.println("========================");
  
  // เริ่มต้นการสื่อสาร I2C
  Wire.begin(D2, D1);  // SDA, SCL
  
  // เริ่มต้นโมดูล RTC
  if (!rtc.begin()) {
    Serial.println("ไม่พบอุปกรณ์ RTC!");
    while (1);
  }

  // ถ้าต้องการตั้งเวลาอัตโนมัติเมื่อแบตเตอรี่หมด
  if (rtc.lostPower()) {
    Serial.println("RTC แบตหมด กำลังตั้งเวลาใหม่!");
    // ตั้งเวลาเป็นเวลาคอมไพล์
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  Serial.println("------------------------");
  Serial.print("ค่า LDR Threshold: ");
  Serial.println(LDR_THRESHOLD);
  Serial.print("ดีเลย์เปิดไฟ: ");
  Serial.print(LIGHT_ON_DELAY / 1000);
  Serial.println(" วินาที");
  Serial.print("ดีเลย์ปิดไฟ: ");
  Serial.print(LIGHT_OFF_DELAY / 1000);
  Serial.println(" วินาที");

  pinMode(LDR_PIN, INPUT);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  
  // ตั้งค่าเริ่มต้นให้ปิดรีเลย์
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  
  // เริ่มการเชื่อมต่อ WiFi
  Serial.print("กำลังเชื่อมต่อ WiFi");
  WiFi.begin(ssid, password);

  // รอการเชื่อมต่อ WiFi
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("เชื่อมต่อ WiFi สำเร็จ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // เริ่มการเชื่อมต่อ Blynk
  Serial.println("กำลังเชื่อมต่อกับ Blynk...");
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);
  Serial.println("เชื่อมต่อกับ Blynk สำเร็จ");

  // ตั้งค่า BlynkTimer
  timer.setInterval(10000L, checkTime);
  
  Serial.println("การตั้งค่าเริ่มต้นเสร็จสมบูรณ์");
  Serial.println("ระบบเริ่มทำงานแล้ว");
  Serial.println("========================");
}

void loop() {
  Blynk.run();
  timer.run();
  
  // ตรวจสอบการเชื่อมต่อ WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi หลุด กำลังเชื่อมต่อใหม่...");
    WiFi.reconnect();
  }
  
  // ตรวจสอบการเชื่อมต่อ Blynk
  if (!Blynk.connected()) {
    Serial.println("Blynk หลุด กำลังเชื่อมต่อใหม่...");
    Blynk.connect();
  }
  
  // จัดการเวลาและรีเลย์
  manageTimerRelay();
  
  // ควบคุมรีเลย์ตามแสง
  controlRelayByLight();
  
  delay(500);
}

// Blynk Connected Event
BLYNK_CONNECTED() {
  Serial.println("เชื่อมต่อกับ Blynk สำเร็จ");
  Blynk.sendInternal("rtc", "sync");
  
  // อัพเดตค่าต่างๆ ไปยัง Blynk เมื่อเชื่อมต่อ
  Blynk.virtualWrite(V0, relay1State);
  Blynk.virtualWrite(V1, relay2State);
  Blynk.virtualWrite(V8, relay1State);
  Blynk.virtualWrite(V9, relay2State);
  Blynk.virtualWrite(V10, analogRead(LDR_PIN));
  Blynk.virtualWrite(V11, LDR_THRESHOLD);
  Blynk.virtualWrite(V12, LIGHT_ON_DELAY / 1000);
  Blynk.virtualWrite(V13, LIGHT_OFF_DELAY / 1000);
  
  Serial.println("อัพเดตข้อมูลไปยัง Blynk เรียบร้อย");
}
