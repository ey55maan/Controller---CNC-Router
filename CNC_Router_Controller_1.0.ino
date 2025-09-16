#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>

// Pin definitions
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TFT_CS     15
#define TFT_DC     2
#define TFT_RST    -1
#define TFT_BL     27
#define SD_CS      5

// SPI bus definitions
#define VSPI_SCK   14
#define VSPI_MOSI  13
#define VSPI_MISO  12
#define HSPI_SCK   18
#define HSPI_MOSI  23
#define HSPI_MISO  19

SPIClass dispSPI(VSPI);
SPIClass sdSPI(HSPI);

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

HardwareSerial SerialCNC(2);  // UART2 for CNC

String cncBuffer = "";
String lastLine = "";

// States
enum State { FILE_LIST, FILE_CONFIRM, STREAMING, COMPLETION };
State currentState = FILE_LIST;

String selectedFile = "";
File gcodeFile;
long fileSize = 0;
long sentBytes = 0;
int totalLines = 0;
int sentLines = 0;
bool waitingForResponse = false;

#define TFT_GREY 0x8410
#define BUTTON_WHITE 0xFFFF
#define BUTTON_LIGHTGREY 0xC618

// ---------------- Buttons ----------------
struct Button {
  int x, y, w, h;
  String label;
  bool pressed;
  uint16_t normalColor = BUTTON_WHITE;
};

// Larger button sizes
Button homeBtn  = {20, 20, 140, 120, "HOME", false};
Button probeBtn = {20, 160, 140, 120, "SET TOOL\nHEIGHT", false};
Button scrollUp  = {400, 0, 80, 40, "UP", false};
Button scrollDown = {400, 260, 80, 40, "DN", false};

Button runBtn = {100, 240, 120, 60, "Run", false, TFT_GREEN};
Button returnBtn = {260, 240, 120, 60, "Return", false};
Button restartBtn = {100, 240, 120, 60, "Restart", false, TFT_GREEN};

Button resumeBtn = {50, 180, 120, 100, "Resume", false, TFT_GREEN};
Button holdBtn = {180, 180, 120, 100, "Hold", false, TFT_YELLOW};
Button stopBtn = {310, 180, 120, 100, "Stop", false, TFT_RED};

#define MAX_FILES 50
#define VISIBLE_FILES 5
Button fileBtns[VISIBLE_FILES];
String fileNames[MAX_FILES];
String fullFileNames[MAX_FILES];
int fileCount = 0;
int scrollIndex = 0;

// ---------------- Drawing Functions ----------------
void drawButton(Button &btn) {
  uint16_t fillColor = btn.pressed ? BUTTON_LIGHTGREY : btn.normalColor;
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, fillColor);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, TFT_BLACK);

  tft.setTextColor(TFT_BLACK, fillColor);
  tft.setTextSize(2);

  String text = btn.label;
  int lines = 1;
  for (int i = 0; i < text.length(); i++) if (text[i] == '\n') lines++;

  int lineHeight = 20;
  int totalTextHeight = lines * lineHeight;
  int16_t yStart = btn.y + (btn.h - totalTextHeight) / 2 + 4;

  int lineIndex = 0;
  int lastPos = 0;
  for (int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text[i] == '\n') {
      String line = text.substring(lastPos, i);
      int16_t xStart = btn.x + (btn.w - line.length() * 12) / 2;
      tft.setCursor(xStart, yStart + lineIndex * lineHeight);
      tft.print(line);
      lastPos = i + 1;
      lineIndex++;
    }
  }
}

bool insideButton(Button &btn, int x, int y) {
  return (x > btn.x && x < (btn.x + btn.w) && y > btn.y && y < (btn.y + btn.h));
}

void readSDFiles() {
  sdSPI.begin(HSPI_SCK, HSPI_MISO, HSPI_MOSI, SD_CS);
  sdSPI.setFrequency(2000000);

  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("SD card init failed!");
    return;
  }

  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory!");
    return;
  }

  fileCount = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && fileCount < MAX_FILES) {
      String fileName = String(entry.name());
      fullFileNames[fileCount] = fileName;
      if (fileName.length() > 23) fileNames[fileCount] = fileName.substring(0, 23);
      else fileNames[fileCount] = fileName;
      fileCount++;
    }
    entry.close();
  }
  root.close();
  Serial.println("SD card files read: " + String(fileCount));
}

void drawFileButtons() {
  int btnH = 44;
  int startY = 40;
  for (int i = 0; i < VISIBLE_FILES; i++) {
    int idx = scrollIndex + i;
    if (idx >= fileCount) {
      fileBtns[i].label = "";
      fileBtns[i].pressed = false;
      tft.fillRect(170, startY + i * btnH, 290, btnH, TFT_GREY);
      tft.drawRect(170, startY + i * btnH, 290, btnH, TFT_BLACK);
      continue;
    }
    fileBtns[i].x = 170;
    fileBtns[i].y = startY + i * btnH;
    fileBtns[i].w = 290;
    fileBtns[i].h = btnH;
    fileBtns[i].label = fileNames[idx];
    drawButton(fileBtns[i]);
  }
}

void drawFileListScreen() {
  tft.fillScreen(TFT_GREY);
  drawButton(homeBtn);
  drawButton(probeBtn);
  drawButton(scrollUp);
  drawButton(scrollDown);
  drawFileButtons();
}

void drawConfirmScreen(String file) {
  tft.fillScreen(TFT_GREY);
  tft.setTextColor(TFT_BLACK, BUTTON_WHITE);
  tft.setTextSize(2);

  const int maxChars = 30;
  int numLines = (file.length() + maxChars - 1) / maxChars;
  int lineHeight = 20;
  int totalTextHeight = numLines * lineHeight;
  int boxW = maxChars * 12 + 20;
  int boxH = totalTextHeight + 10;
  int boxX = (480 - boxW) / 2;
  int boxY = 80;

  tft.fillRect(boxX, boxY, boxW, boxH, BUTTON_WHITE);
  tft.drawRect(boxX, boxY, boxW, boxH, TFT_BLACK);

  int yText = boxY + 5;
  for (int i = 0; i < numLines; i++) {
    String line = file.substring(i * maxChars, min((i + 1) * maxChars, (int)file.length()));
    tft.setCursor(boxX + 10, yText);
    tft.print(line);
    yText += lineHeight;
  }

  int btnY = boxY + boxH + 20;
  runBtn.y = btnY;
  returnBtn.y = btnY;
  drawButton(runBtn);
  drawButton(returnBtn);
}

void drawStreamingScreen(String file) {
  tft.fillScreen(TFT_GREY);
  tft.setTextColor(TFT_BLACK, TFT_GREY);
  tft.setTextSize(2);
  String runningText = "Running: " + file;
  tft.setCursor((480 - runningText.length() * 12) / 2, 100);
  tft.print(runningText);
  drawButton(resumeBtn);
  drawButton(holdBtn);
  drawButton(stopBtn);
  tft.drawRect(20, 20, 440, 20, TFT_BLACK);
  updateProgress();
}

void updateProgress() {
  if (fileSize > 0) {
    float progress = (float)sentBytes / fileSize;
    tft.fillRect(20, 20, progress * 440, 20, TFT_BLUE);

    // Overlay progress text
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK, TFT_GREY);
    String progressText = String(sentLines) + "/" + String(totalLines) + " lines";
    progressText += " (" + String(int(progress * 100)) + "%)";
    int textWidth = progressText.length() * 12;
    tft.setCursor(20 + (440 - textWidth) / 2, 50);
    tft.print(progressText);
  }
}

void drawCompletionScreen(String file) {
  tft.fillScreen(TFT_GREY);
  tft.setTextColor(TFT_BLACK, BUTTON_WHITE);
  tft.setTextSize(2);

  String message = "File Completed: " + file;
  const int maxChars = 30;
  int numLines = (message.length() + maxChars - 1) / maxChars;
  int lineHeight = 20;
  int totalTextHeight = numLines * lineHeight;
  int boxW = maxChars * 12 + 20;
  int boxH = totalTextHeight + 10;
  int boxX = (480 - boxW) / 2;
  int boxY = 80;

  tft.fillRect(boxX, boxY, boxW, boxH, BUTTON_WHITE);
  tft.drawRect(boxX, boxY, boxW, boxH, TFT_BLACK);

  int yText = boxY + 5;
  for (int i = 0; i < numLines; i++) {
    String line = message.substring(i * maxChars, min((i + 1) * maxChars, (int)message.length()));
    tft.setCursor(boxX + 10, yText);
    tft.print(line);
    yText += lineHeight;
  }

  int btnY = boxY + boxH + 20;
  restartBtn.y = btnY;
  returnBtn.y = btnY;
  drawButton(restartBtn);
  drawButton(returnBtn);
}

Button* getButtonAt(int x, int y) {
  if (currentState == FILE_LIST) {
    if (insideButton(homeBtn, x, y)) return &homeBtn;
    if (insideButton(probeBtn, x, y)) return &probeBtn;
    if (insideButton(scrollUp, x, y)) return &scrollUp;
    if (insideButton(scrollDown, x, y)) return &scrollDown;
    for (int i = 0; i < VISIBLE_FILES; i++) if (fileBtns[i].label != "" && insideButton(fileBtns[i], x, y)) return &fileBtns[i];
  } else if (currentState == FILE_CONFIRM) {
    if (insideButton(runBtn, x, y)) return &runBtn;
    if (insideButton(returnBtn, x, y)) return &returnBtn;
  } else if (currentState == STREAMING) {
    if (insideButton(resumeBtn, x, y)) return &resumeBtn;
    if (insideButton(holdBtn, x, y)) return &holdBtn;
    if (insideButton(stopBtn, x, y)) return &stopBtn;
  } else if (currentState == COMPLETION) {
    if (insideButton(restartBtn, x, y)) return &restartBtn;
    if (insideButton(returnBtn, x, y)) return &returnBtn;
  }
  return nullptr;
}

void drawStatusLine(String line) {
  int y = 300;
  tft.fillRect(0, y, 480, 20, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(2, y + 4);
  tft.print(line);
}


void setup() {
  Serial.begin(115200);
  SerialCNC.begin(115200, SERIAL_8N1, 35, 1);   //was 35, 1);

  delay(1000);
  SerialCNC.println("?");
  Serial.println("Sent test: ?");

  readSDFiles();

  dispSPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, TFT_CS);
  dispSPI.setFrequency(2000000);

  tft.begin();
  tft.setRotation(1);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  drawFileListScreen();

  if (ts.begin(dispSPI)) Serial.println("Touchscreen OK");
  else Serial.println("Touchscreen failed to start!");
  ts.setRotation(1);
}

void loop() {
  while (SerialCNC.available()) {
    char c = SerialCNC.read();
    Serial.print(c);
    if (c == '\n') {
      lastLine = cncBuffer;
      cncBuffer = "";
      Serial.println("CNC: " + lastLine);
      drawStatusLine(lastLine);
      if (currentState == STREAMING && waitingForResponse && (lastLine == "ok" || lastLine.startsWith("error:"))) {
        waitingForResponse = false;
      }
    } else if (c != '\r') cncBuffer += c;
  }

  if (currentState == STREAMING && !waitingForResponse) {
    if (gcodeFile.available()) {
      String line = gcodeFile.readStringUntil('\n');
      sentBytes = gcodeFile.position();
      sentLines++;
      updateProgress();
      line.trim();
      int commentPos = line.indexOf(';');
      if (commentPos >= 0) line = line.substring(0, commentPos);
      line.trim();
      if (line.length() > 0) {
        SerialCNC.println(line);
        Serial.println("Sent G: " + line);
        waitingForResponse = true;
      }
    } else if (!waitingForResponse) {
      gcodeFile.close();
      currentState = COMPLETION;
      drawCompletionScreen(selectedFile);
    }
  }

  static bool wasTouched = false;
  static Button* currentHighlighted = nullptr;
  bool isTouched = ts.touched();

  if (isTouched) {
    TS_Point p = ts.getPoint();
    int mapX = map(p.x, 300, 3900, 480, 0);
    int mapY = map(p.y, 300, 3900, 320, 0);
    Button* btn = getButtonAt(mapX, mapY);
    if (btn != currentHighlighted) {
      if (currentHighlighted) { currentHighlighted->pressed = false; drawButton(*currentHighlighted); }
      if (btn) { btn->pressed = true; drawButton(*btn); }
      currentHighlighted = btn;
    }
  } else if (wasTouched) {
    if (currentHighlighted) {
      currentHighlighted->pressed = false;
      drawButton(*currentHighlighted);

      if (currentState == FILE_LIST) {
        if (currentHighlighted == &homeBtn) { SerialCNC.println("$H"); Serial.println("Sent: $H"); }
        else if (currentHighlighted == &probeBtn) { SerialCNC.println("$RM=0"); Serial.println("Sent: $RM=0"); }
        else if (currentHighlighted == &scrollUp) { if (scrollIndex>0) scrollIndex--; drawFileButtons(); }
        else if (currentHighlighted == &scrollDown) { if (scrollIndex+VISIBLE_FILES<fileCount) scrollIndex++; drawFileButtons(); }
        else { for (int i=0;i<VISIBLE_FILES;i++) if (currentHighlighted==&fileBtns[i]) { selectedFile=fullFileNames[scrollIndex+i]; currentState=FILE_CONFIRM; drawConfirmScreen(selectedFile); break; } }
      } else if (currentState == FILE_CONFIRM) {
        if (currentHighlighted==&runBtn) {
          gcodeFile=SD.open("/"+selectedFile);
          Serial.println("Opening: /"+selectedFile);
          if (gcodeFile) { fileSize=gcodeFile.size(); sentBytes=0; sentLines=0; totalLines=0;
            // Count total lines
            File temp=SD.open("/"+selectedFile);
            while(temp.available()){ if(temp.readStringUntil('\n').length()>0) totalLines++; }
            temp.close();
            waitingForResponse=false; currentState=STREAMING; drawStreamingScreen(selectedFile); 
          } else { Serial.println("Failed to open file"); currentState=FILE_LIST; drawFileListScreen(); }
        } else if (currentHighlighted==&returnBtn) { currentState=FILE_LIST; drawFileListScreen(); }
      } else if (currentState == STREAMING) {
        if (currentHighlighted==&resumeBtn) { SerialCNC.write('~'); Serial.println("Sent: ~ (Resume)"); }
        else if (currentHighlighted==&holdBtn) { SerialCNC.write('!'); Serial.println("Sent: ! (Hold)"); }
        else if (currentHighlighted==&stopBtn) { SerialCNC.write(0x18); Serial.println("Sent: 0x18 (Abort)"); delay(100);
          while(SerialCNC.available()){ Serial.print((char)SerialCNC.read()); }
          gcodeFile.close(); currentState=FILE_LIST; drawFileListScreen();
        }
      } else if (currentState == COMPLETION) {
        if (currentHighlighted==&restartBtn) {
          gcodeFile=SD.open("/"+selectedFile);
          Serial.println("Restarting: /"+selectedFile);
          if (gcodeFile) {
            sentBytes=0;
            sentLines=0;
            waitingForResponse=false;
            currentState=STREAMING;
            drawStreamingScreen(selectedFile);
          } else {
            Serial.println("Failed to open file for restart");
            currentState=FILE_LIST;
            drawFileListScreen();
          }
        } else if (currentHighlighted==&returnBtn) { currentState=FILE_LIST; drawFileListScreen(); }
      }
      currentHighlighted=nullptr;
    }
  }

  wasTouched = isTouched;
  delay(50);
}