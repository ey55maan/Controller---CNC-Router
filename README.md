FluidNC CNC Controller and TFT Display Interface

Overview
This project combines FluidNC firmware with an ESP32 based touchscreen display interface for CNC machine control. It allows the user to run G code from an SD card, monitor job progress, and send commands through a graphical interface on a TFT screen with touch input. The design provides a standalone CNC experience without requiring a computer connection while still supporting remote control through serial or network if needed.

Features
The FluidNC firmware is GRBL compatible and runs on ESP32 hardware. It supports multiple axes, homing, probing, spindle control, and is configured through YAML files.
The touchscreen display provides a graphical menu system, SD card file browsing, job streaming with progress indication, spindle and coolant controls, confirmation dialogs, error reporting, and a status bar that shows machine messages. At the end of a program the display waits for FluidNC to signal completion and then shows a program complete screen.
Integration is done through serial communication between the FluidNC controller and the display controller. The system handles pause, resume, and stop, and can display coordinate system information.

Hardware Requirements
The controller requires an ESP32 development board for FluidNC, stepper motor drivers such as DRV8825 or TMC2208, stepper motors, a CNC frame, and a spindle or router with relay, VFD, or PWM control.
The display requires a second ESP32 board, a TFT LCD with touch input such as an ILI9341 or ILI9488, an XPT2046 touch controller, and an SD card slot.
The two ESP32 boards communicate over a serial connection. Power requirements are 5V for logic and a separate power supply for motors sized appropriately for the CNC machine.

Functionality
At startup FluidNC loads the YAML configuration and the display initializes into the main menu. The user can browse the SD card for G code files and select one to run. While the program runs, the screen shows the active file, spindle state, machine status, and a progress bar with line counts. Messages from FluidNC are displayed on the status line. When the program finishes, signaled by an M2, M30, or cycle complete message, the display shows a completion screen and offers options to rerun or return to the menu.

Repository Structure
The firmware directory contains FluidNC YAML configuration and firmware build files.
The display directory contains the Arduino or PlatformIO sketch for the TFT display controller.
This README file describes the project.

Getting Started
First flash FluidNC onto an ESP32 and load your machine YAML configuration.
Next upload the display sketch onto a second ESP32 with the TFT screen attached.
Wire a serial connection between the two ESP32 boards.
Insert an SD card containing G code files into the display board.
Power up both boards and the CNC can be operated directly from the touchscreen.

License
This project is released under the MIT License.
