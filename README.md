# DCBM

DCBM (Dynamic Continuous Biosensing Monitor) is a portable biosensing platform designed for automated electrochemical measurements and wireless data acquisition.

This repository contains the complete firmware and software stack used for automated electrochemical measurements, wireless communication, and mobile data visualization.


The system consists of:

- Android application for user control and data visualization
- BLE bridge firmware running on STM32-H2-MINI
- PIC-based electrochemical control unit
- LMP91000 analog front-end
- Multiplexer (MUX) controlled electrode selection

The platform enables automated monitoring, data collection, and wireless communication between electrochemical sensors and a mobile device.

---

## Scientific Background

This platform was developed as part of the work described in:

**Keren Zhou, Minwoo Kim, Chia-Wei Liu, Jack W. Harbell, Amit K. Mathur, Rafael Nunez Nateras, Vadim Jucaud, Mehmet R. Dokmeci, Xiling Shen, Bashar A. Aqel, Joseph Wang, Michelle C. Nguyen, and Yangzhi Zhu**

**A Clinically Deployed Dual-Compartment Biochemical Monitoring Platform for Human Liver Perfusion**

The system enables real-time biochemical monitoring of both perfusate and bile compartments during normothermic machine perfusion of human donor livers, providing continuous physiological information that may assist in organ assessment and transplantation research.

For scientific details regarding the sensing strategy, experimental design, and clinical deployment, please refer to the publication above.

---


---

## System Architecture

```text
┌─────────────────┐
│  Android App    │
│ (SmartOrganApp) │
└────────┬────────┘
         │ BLE
         ▼
┌─────────────────┐
│ STM32-H2-MINI   │
│ BLE Bridge      │
└────────┬────────┘
         │ UART
         ▼
┌─────────────────┐
│ PIC MCU         │
│ Measurement     │
│ Controller      │
└────────┬────────┘
         │
 ┌───────┴────────┐
 │                │
 ▼                ▼
MUX           LMP91000
 │                │
 └───────┬────────┘
         ▼
Electrochemical Sensors
```

---

## Repository Structure

```text
DCBM/
│
├── BLE_PIC_Bridge/
│   ├── STM32-H2-MINI firmware
│   ├── BLE communication stack
│   └── UART bridge implementation
│
├── PIC/
│   ├── PIC firmware
│   ├── MUX control
│   ├── LMP91000 control
│   ├── Measurement sequencing
│   └── Sensor monitoring logic
│
├── SmartOrganApp/
│   ├── Android application
│   ├── BLE communication interface
│   ├── User interface
│   └── Data visualization
│
├── LICENSE
├── README.md
└── .gitignore
```

---

## Project Components

### 1. SmartOrganApp

Android application responsible for:

- Connecting to the device through Bluetooth Low Energy (BLE)
- Sending measurement commands
- Receiving sensor data
- Visualizing measurement results
- Logging experimental data

---

### 2. BLE_PIC_Bridge

Firmware running on STM32-H2-MINI.

Responsibilities:

- BLE communication with Android devices
- UART communication with PIC MCU
- Forwarding commands from Android to PIC
- Forwarding measurement data from PIC to Android

The STM32 acts as a transparent communication bridge between the mobile application and the electrochemical control hardware.

---

### 3. PIC Firmware

Main measurement controller.

Responsibilities:

- Controlling MUX channel selection
- Configuring LMP91000 analog front-end
- Executing automated measurement sequences
- Collecting electrochemical sensor signals
- Sending data to STM32 through UART

---

## Communication Flow

### Command Path

```text
Android App
      ↓ BLE
STM32-H2-MINI
      ↓ UART
PIC MCU
      ↓
MUX + LMP91000
```

### Data Path

```text
Sensor Signal
      ↓
PIC MCU
      ↓ UART
STM32-H2-MINI
      ↓ BLE
Android App
```

---

## Hardware Requirements

- STM32-H2-MINI
- PIC Microcontroller
- LMP91000 Potentiostat AFE
- Analog Multiplexer (MUX)
- Electrochemical Sensor
- Android Device

---

## Software Requirements

### Android

- Android Studio
- Android SDK

### STM32

- STM32CubeIDE
- STM32CubeMX

### PIC

- MPLAB X IDE
- XC8 Compiler

---

## Current Features

- BLE communication
- UART bridge
- Remote measurement control
- Automated sensor monitoring
- MUX channel switching
- LMP91000 configuration
- Real-time data transfer
- Mobile data visualization

---

## Future Development

- Multi-channel electrochemical sensing
- Cloud synchronization
- Data logging database
- Real-time analytics
- Advanced sensor calibration
- OTA firmware update

---

## License

This project is released under the MIT License.

See the LICENSE file for details.

---

## Author

Minwoo Kim

Terasaki Institute for Biomedical Innovation (TIBI)

Los Angeles, California, USA
