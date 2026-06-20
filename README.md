# MP-Project
Microprocessors course project: Patient Monitoring System


## 📌 Overview
An embedded C project that transforms a PIC16F877A microcontroller into a real-time, medical patient monitor. 
This system continuously tracks four critical vitals—**Respiration Rate, Heart Rate, and Room Temperature of ICU**—and features a rigid priority alarm engine to alert medical staff to critical emergencies like apnea or cardiac flatline, or Intensive Care Unit (ICU) temprature which can cause contamination when unit temp is high.

This project demonstrates advanced 8-bit microcontroller techniques, bypassing standard `delay()` functions in favor of **hardware timers and interrupts** to ensure life-safety calculations are never blocked by UI or sensor updates.

## ⚙️ Key Features
* **Priority Alarm Engine:** Evaluates vitals in a strict medical hierarchy (e.g., an Apnea alarm will immediately override a High Temperature alarm).
* **Dual-Engine Data Processing:** Uses a "Fast Engine" to trigger instant safety alarms on single beats, and a "Slow Engine" to accumulate 5-second rolling averages to prevent LCD flickering.
* **Non-Blocking Architecture:** Utilizes Timer0 as a background system heartbeat and Timer1 as a precision microsecond stopwatch, ensuring simultaneous sensor polling.
* **Custom I2C Driver:** Includes a fully custom-written 100kHz I2C master driver to interface with the MAX30102 photoplethysmography (PPG) sensor.

## 🛠️ Hardware Requirements
* **Microcontroller:** PIC16F877A 
* **Pulse Oximeter:** MAX30102 (I2C)
* **Respiration Sensor:** HC-SR04 Ultrasonic (Measures chest expansion)
* **Temperature Sensor:** LM35 (Analog)
* **Display:** 16x2 LCD (Wired in 4-bit mode)
* **UI/Warnings:** 1x Push Button (On/Off), 3x LEDs (Alarms), 1x System status LED, 1x Active Buzzer

## 🔌 Pin Mapping
| Component | PIC16F877A Pin | Function |
| :--- | :--- | :--- |
| **MAX30102 (I2C)** | RC3 (SCL) / RC4 (SDA) | Heart Rate |
| **HC-SR04** | RB1 (TRIG) / RB0 (ECHO) | Respiration Tracking |
| **LM35** | RA0 (AN0) | Room Temperature |
| **LCD Data** | RD4, RD5, RD6, RD7 | 4-bit Display Data |
| **LCD Control** | RC0 (RS) / RC1 (E) | Display Control |
| **Alarms** | RD0, RD2, RC5 | Warning LEDs |
| **Buzzer** | RD1 | Audio Alarm |
| **Nurse Button** | RB2 | Standby / Active Toggle | Green LED (RD3)

## 🧠 Code Architecture Highlights
1. **ISR** * Handles Timer0 overflows every 13.1ms for system ticking and background LED flashing.
   * Handles the External Interrupt (RB0) to catch the exact microsecond the ultrasonic sound wave returns.
2. **Bit Masking:** Directly manipulates the `OPTION_REG` to swap between rising-edge and falling-edge detection on the fly without breaking other timer configurations.
3. **Right-Justified ADC:** Uses 10-bit analog-to-digital conversion (`ADRESH` + `ADRESL`) to ensure maximum precision when reading the LM35's 10mV/°C output.

## 🚀 Software / Tools Used
* **IDE:** MPLAB X IDE
* **Compiler:** XC8
* **Simulation:** Proteus Design Suite

## 👨‍💻 Authors
* **Ammar Ahmed** 62240018 - *Biomedical Engineering Undergraduate*
* **Ahmed Elfadil** 62240030 - *Biomedical Engineering Undergraduate*

