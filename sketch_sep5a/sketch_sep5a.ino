#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>

// TFT Pin definitions (adjust these to match your userSetup.h)
#define TFT_CS    15
#define TFT_RST   4
#define TFT_DC    2
#define TFT_MOSI  23
#define TFT_SCLK  18
#define TFT_MISO  19

#define TOUCH_CS 5
#define TOUCH_IRQ 27
#define ECG_PIN 34    

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

const int TOUCH_X_MIN = 598;
const int TOUCH_X_MAX = 3699;
const int TOUCH_Y_MIN = 305;
const int TOUCH_Y_MAX = 3718;

const int SCREEN_WIDTH = 240;
const int SCREEN_HEIGHT = 320;

// Screen states
bool showingHeartRate = false;
bool showingArrhythmia = false;
bool onTitle = true;
bool onMenu = false;
bool onInitializing = false;
bool onECGWaveform = false;
bool onBPMDisplay = false;
bool onArrhythmiaAnalysis = false;

int btnX = 60, btnY = 220, btnW = 120, btnH = 40;
int menu1X = 20, menu1Y = 100, menuW = 200, menuH = 50; 
int menu2X = 20, menu2Y = 160, menu3X = 20, menu3Y = 210;
int backBtnX = 10, backBtnY = 10, backBtnW = 30, backBtnH = 30;

unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 100;

int xPos = 0;
int prevY = 0;
int baseline;

class HeartMonitor {
private:
  static const int SAMPLE_SIZE = 100;
  static const int THRESHOLD = 2048; 
  int samples[SAMPLE_SIZE];
  int sampleIndex = 0;
  unsigned long lastBeatTime = 0;
  unsigned long beatIntervals[10];
  int intervalIndex = 0;
  bool lastAboveThreshold = false;
  int currentBPM = 0;
  
public:
  void addSample(int value) {
    samples[sampleIndex] = value;
    sampleIndex = (sampleIndex + 1) % SAMPLE_SIZE;
    
    bool currentAboveThreshold = (value > THRESHOLD);
    
    if (currentAboveThreshold && !lastAboveThreshold) {
      unsigned long currentTime = millis();
      
      if (lastBeatTime > 0 && (currentTime - lastBeatTime) > 300) { 
        unsigned long interval = currentTime - lastBeatTime;
        beatIntervals[intervalIndex] = interval;
        intervalIndex = (intervalIndex + 1) % 10;
        calculateBPM();
      }
      lastBeatTime = currentTime;
    }
    
    lastAboveThreshold = currentAboveThreshold;
  }
  
  void calculateBPM() {
    unsigned long totalInterval = 0;
    int validIntervals = 0;
    
    for (int i = 0; i < 10; i++) {
      if (beatIntervals[i] > 0) {
        totalInterval += beatIntervals[i];
        validIntervals++;
      }
    }
    
    if (validIntervals >= 3) {
      unsigned long avgInterval = totalInterval / validIntervals;
      currentBPM = 60000 / avgInterval; 
      
      if (currentBPM < 40 || currentBPM > 200) {
        currentBPM = 0;
      }
    }
  }
  
  int getBPM() {
    return currentBPM;
  }
  
  void reset() {
    sampleIndex = 0;
    intervalIndex = 0;
    lastBeatTime = 0;
    currentBPM = 0;
    lastAboveThreshold = false;
    for (int i = 0; i < 10; i++) {
      beatIntervals[i] = 0;
    }
  }
};

HeartMonitor heartMonitor;

unsigned long initStartTime = 0;
unsigned long ecgStartTime = 0;
unsigned long bpmUpdateTime = 0;
unsigned long arrhythmiaStartTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  Serial.println("Starting Arrhythmia Detector...");
  
  // Initialize TFT display
  Serial.println("Initializing TFT display...");
  tft.init();
  
  // Test if display is working
  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_GREEN);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);
  
  tft.setRotation(0);  
  tft.fillScreen(TFT_BLACK);
  Serial.println("TFT display initialized successfully!");
  
  // Initialize touchscreen
  Serial.println("Initializing touchscreen...");
  ts.begin();
  ts.setRotation(0);  
  Serial.println("Touchscreen initialized!");

  Title();
  
  Serial.println("Arrhythmia Detector Ready!");
}

void loop() {
  handleTouch();

  if (onInitializing) {
    if (showingHeartRate && millis() - initStartTime >= 4000) { 
      startECGWaveform();
    }
    else if (showingArrhythmia && millis() - initStartTime >= 2000) {
      startArrhythmiaWaveform();
    }
  }
  else if (onECGWaveform) {
    updateHeartWave();
    if (showingHeartRate && millis() - ecgStartTime >= 25000) { 
      showBPMScreen();
    }
    else if (showingArrhythmia && millis() - ecgStartTime >= 22000) {
      showArrhythmiaResults();
    }
  }
  else if (onBPMDisplay) {
    if (millis() - bpmUpdateTime >= 2000) {
      updateBPMDisplay();
      bpmUpdateTime = millis();
    }
    int ecgValue = analogRead(ECG_PIN);
    heartMonitor.addSample(ecgValue);
  }
  else if (onArrhythmiaAnalysis) {
    if (millis() - bpmUpdateTime >= 2000) {
      updateArrhythmiaDisplay();
      bpmUpdateTime = millis();
    }
    int ecgValue = analogRead(ECG_PIN);
    heartMonitor.addSample(ecgValue);
  }
}

void handleTouch() {
  if (ts.touched()) {
    unsigned long currentTime = millis();
    if (currentTime - lastTouchTime < TOUCH_DEBOUNCE) return;
    lastTouchTime = currentTime;
    
    TS_Point p = ts.getPoint();
    int x = map(p.x, TOUCH_X_MAX, TOUCH_X_MIN, 0, SCREEN_WIDTH);   
    int y = map(p.y, TOUCH_Y_MAX, TOUCH_Y_MIN, 0, SCREEN_HEIGHT);  
    x = constrain(x, 0, SCREEN_WIDTH - 1);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);
    
    if (onTitle) {
      if (x > btnX && x < (btnX + btnW) && y > btnY && y < (btnY + btnH)) {
        showMenu();
      }
    }
    else if (onMenu) {
      if (x < 40 && y < 40) { Title(); return; }
      if (x > menu1X && x < (menu1X + menuW) && y > menu1Y && y < (menu1Y + menuH)) {
        showArrhythmiaDetector();
      }
      else if (x > menu2X && x < (menu2X + menuW) && y > menu2Y && y < (menu2Y + menuH)) {
        startHeartRateSequence();
      }
      else if (x > menu3X && x < (menu3X + menuW) && y > menu3Y && y < (menu3Y + menuH)) {
        showAbout();
      }
    }
    else if (onBPMDisplay || onArrhythmiaAnalysis) {
      if (x > backBtnX && x < (backBtnX + backBtnW) && y > backBtnY && y < (backBtnY + backBtnH)) {
        resetHeartRateState();
        showMenu();
      }
    }
    else {
      if (x < 40 && y < 40) {
        resetHeartRateState();
        showMenu();
      }
    }
  }
}

void resetHeartRateState() {
  showingHeartRate = false;
  showingArrhythmia = false;
  onInitializing = false;
  onECGWaveform = false;
  onBPMDisplay = false;
  onArrhythmiaAnalysis = false;
  heartMonitor.reset();
}

void Title() {
  onTitle = true;
  onMenu = false;
  resetHeartRateState();
  
  Serial.println("Drawing title screen...");
  
  tft.fillScreen(TFT_BLACK);
  
  // Draw centered text for "Arrhythmia"
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  String text = "Arrhythmia";
  int textWidth = text.length() * 18; // Approximate width for size 3
  int x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 40);
  tft.println(text);
  
  // Draw centered text for "Detector"
  text = "Detector";
  textWidth = text.length() * 18;
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 80);
  tft.println(text);
  
  // Draw centered text for "by Suhana & Simran"
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  text = "by Suhana & Simran";
  textWidth = text.length() * 6; // Approximate width for size 1
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 120);
  tft.println(text);
  
  // Draw heart shape
  int heart[7][8] = {
    {0,1,1,0,0,1,1,0},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {0,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,0,0},
    {0,0,0,1,1,0,0,0}
  };
  
  int startX = 88; // Centered for 240px width
  int startY = 150; 
  int pixelSize = 6; 
  
  for(int row = 0; row < 7; row++) {
    for(int col = 0; col < 8; col++) {
      if(heart[row][col] == 1) {
        tft.fillRect(startX + col * pixelSize, startY + row * pixelSize, 
                     pixelSize, pixelSize, TFT_CYAN);
      }
    }
  }
  
  // Draw button
  int buttonX = 45;
  int buttonY = 220;
  int buttonW = 150;
  int buttonH = 40;
  
  tft.fillRect(buttonX, buttonY, buttonW, buttonH, TFT_CYAN);
  tft.drawRect(buttonX, buttonY, buttonW, buttonH, TFT_WHITE);
  
  tft.setTextColor(TFT_BLACK, TFT_CYAN);
  tft.setTextSize(2);
  text = "Continue";
  textWidth = text.length() * 12; // Approximate width for size 2
  x = buttonX + (buttonW - textWidth) / 2;
  int y = buttonY + (buttonH - 16) / 2; // 16 is approximate height for size 2
  tft.setCursor(x, y);
  tft.println(text);
  
  // Update button coordinates for touch detection
  btnX = buttonX;
  btnY = buttonY;
  btnW = buttonW;
  btnH = buttonH;
  
  Serial.println("Title screen drawn successfully!");
}

void showMenu() {
  onTitle = false;
  onMenu = true;
  resetHeartRateState();
  
  tft.fillScreen(TFT_BLACK);
  
  // Back button (boxed style)
  tft.drawRect(backBtnX, backBtnY, backBtnW, backBtnH, TFT_CYAN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(backBtnX + 8, backBtnY + 8);
  tft.println("<");
  
  // Center "Main Menu" title
  String mainMenuText = "Main Menu";
  int textWidth = mainMenuText.length() * 12; // Approximate width for size 2
  int x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 40); // Moved down a few pixels and centered
  tft.println(mainMenuText);
  
  // Menu option 1: Arrhythmia Detector
  tft.drawRect(menu1X, menu1Y, menuW, menuH, TFT_CYAN);
  
  // Center "1. Arrhythmia" text
  String option1Line1 = "1. Arrhythmia";
  textWidth = option1Line1.length() * 12;
  x = menu1X + (menuW - textWidth) / 2;
  tft.setCursor(x, menu1Y + 8);
  tft.println(option1Line1);
  
  // Center "Detector" text
  String option1Line2 = "Detector";
  textWidth = option1Line2.length() * 12;
  x = menu1X + (menuW - textWidth) / 2;
  tft.setCursor(x, menu1Y + 28);
  tft.println(option1Line2);
  
  // Menu option 2: Heart Rate
  tft.drawRect(menu2X, menu2Y, menuW, menuH, TFT_CYAN);
  
  // Center "2. Heart Rate" text
  String option2Text = "2. Heart Rate";
  textWidth = option2Text.length() * 12;
  x = menu2X + (menuW - textWidth) / 2;
  int y = menu2Y + (menuH - 16) / 2; // Center vertically in the box
  tft.setCursor(x, y);
  tft.println(option2Text);
  
  // Menu option 3: About
  tft.drawRect(menu3X, menu3Y, menuW, menuH, TFT_CYAN);
  
  // Center "3. About" text
  String option3Text = "3. About";
  textWidth = option3Text.length() * 12;
  x = menu3X + (menuW - textWidth) / 2;
  y = menu3Y + (menuH - 16) / 2; // Center vertically in the box
  tft.setCursor(x, y);
  tft.println(option3Text);
}

void showArrhythmiaDetector() {
  onMenu = false;
  onInitializing = true;
  showingArrhythmia = true;
  resetHeartRateState();
  showingArrhythmia = true; // Reset this after resetHeartRateState
  onInitializing = true;
  
  tft.fillScreen(TFT_BLACK);
  drawRedHeart();
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  // Center "Arrhythmia" text
  String line1 = "Arrhythmia";
  int textWidth = line1.length() * 12;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 200);
  tft.println(line1);
  
  // Center "Detection" text
  String line2 = "Detection";
  textWidth = line2.length() * 12;
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 220);
  tft.println(line2);
  
  // Center "Starting..." text
  String line3 = "Starting...";
  textWidth = line3.length() * 12;
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 250);
  tft.println(line3);
  
  initStartTime = millis();
  heartMonitor.reset();
}

void startHeartRateSequence() {
  onMenu = false;
  onInitializing = true;
  showingHeartRate = true;
  
  tft.fillScreen(TFT_BLACK);
  drawRedHeart();
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  // Center "Heart Rate" text
  String line1 = "Heart Rate";
  int textWidth = line1.length() * 12;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 200);
  tft.println(line1);
  
  // Center "Monitoring" text
  String line2 = "Monitoring";
  textWidth = line2.length() * 12;
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 220);
  tft.println(line2);
  
  // Center "Starting..." text
  String line3 = "Starting...";
  textWidth = line3.length() * 12;
  x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 250);
  tft.println(line3);
  
  initStartTime = millis();
  heartMonitor.reset();
}

void drawRedHeart() {
  int heart[7][8] = {
    {0,1,1,0,0,1,1,0},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {0,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,0,0},
    {0,0,0,1,1,0,0,0}
  };
  int startX = 88, startY = 80, pixelSize = 8;
  for(int row = 0; row < 7; row++) {
    for(int col = 0; col < 8; col++) {
      if(heart[row][col] == 1) {
        tft.fillRect(startX + col * pixelSize, startY + row * pixelSize, 
                     pixelSize, pixelSize, TFT_RED); 
      }
    }
  }
}

void startECGWaveform() {
  onInitializing = false;
  onECGWaveform = true;
  
  tft.fillScreen(TFT_BLACK);
  
  baseline = tft.height() / 2;
  drawGrid();
  prevY = baseline;
  xPos = 0;
  ecgStartTime = millis();
}

void updateHeartWave() {
  int ecgValue = analogRead(ECG_PIN);
  heartMonitor.addSample(ecgValue); 
  
  int y = map(ecgValue, 0, 4095, tft.height() - 20, 20); 
  
  if (xPos > 0) {
    if (xPos % 20 == 0) {
      tft.drawLine(xPos, 0, xPos, tft.height(), TFT_DARKGREY);
    } else {
      tft.drawLine(xPos, 0, xPos, tft.height(), TFT_BLACK);
    }
    
    
    tft.drawPixel(xPos, baseline, TFT_WHITE);
  }
  
  
  if (xPos > 0) {
    tft.drawLine(xPos - 1, prevY, xPos, y, TFT_GREEN);
  }
  prevY = y;

  xPos++;
  if (xPos >= tft.width()) {
    xPos = 0;
    drawGrid(); 
  }

  delay(10); 
}

void showBPMScreen() {
  onECGWaveform = false;
  onBPMDisplay = true;
  
  tft.fillScreen(TFT_BLACK);
  
  tft.drawRect(backBtnX, backBtnY, backBtnW, backBtnH, TFT_CYAN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(backBtnX + 8, backBtnY + 8);
  tft.println("<");
  
  drawRedHeart();
  
  bpmUpdateTime = millis();
  updateBPMDisplay();
}

void updateBPMDisplay() {
  tft.fillRect(0, 200, tft.width(), 80, TFT_BLACK);
  
  int bpm = heartMonitor.getBPM();
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  
  if (bpm > 0) {
    tft.setCursor(80, 220);
    tft.println("BPM:");
    tft.setCursor(100, 250);
    tft.println(bpm);
    
    Serial.print("Current BPM: ");
    Serial.println(bpm);
  } else {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    String calcText = "Calculating...";
    int textWidth = calcText.length() * 12; // Approximate width for size 2
    int x = (SCREEN_WIDTH - textWidth) / 2;
    tft.setCursor(x, 230);
    tft.println(calcText);
  }
}

void drawGrid() {
  for (int x = 0; x < tft.width(); x += 20) {
    tft.drawLine(x, 0, x, tft.height(), TFT_DARKGREY);
  }
  for (int y = 0; y < tft.height(); y += 20) {
    tft.drawLine(0, y, tft.width(), y, TFT_DARKGREY);
  }
  tft.drawLine(0, baseline, tft.width(), baseline, TFT_WHITE);
}

void showAbout() {
  onMenu = false;
  resetHeartRateState();
  
  tft.fillScreen(TFT_BLACK);
  
  // Back button (boxed style)
  tft.drawRect(backBtnX, backBtnY, backBtnW, backBtnH, TFT_CYAN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(backBtnX + 8, backBtnY + 8);
  tft.println("<");
  
  // Title - centered with decorative elements
  String titleText = "About Arrhythmia";
  int textWidth = titleText.length() * 12; // Approximate width for size 2
  int x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, 40);
  tft.println(titleText);
  
  // Decorative line under title
  tft.drawLine(40, 60, 200, 60, TFT_CYAN);
  
  // Information text with enhanced colors
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  
  // What is Arrhythmia section
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 75); tft.println("What is Arrhythmia?");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 87); tft.println("An irregular heartbeat pattern");
  tft.setCursor(10, 97); tft.println("that can be dangerous if untreated.");
  
  // Classifications with color coding
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 112); tft.println("Heart Rate Classifications:");
  
  // Normal - Green
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 125); tft.print("• Normal Sinus Rhythm: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("60-100 BPM");
  
  // Bradycardia - Yellow/Orange
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 137); tft.print("• Bradycardia: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("< 60 BPM (Too Slow)");
  
  // Tachycardia - Red
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, 149); tft.print("• Tachycardia: ");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.println("> 100 BPM (Too Fast)");
  
  // ECG Technology section
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(10, 167); tft.println("ECG Technology:");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 179); tft.println("• AD8232 sensor monitors QRS");
  tft.setCursor(10, 189); tft.println("• Pan-Tompkins algorithm detects");
  tft.setCursor(10, 199); tft.println("  R-peaks for accurate BPM");
  tft.setCursor(10, 209); tft.println("• Real-time analysis & classification");
  
  // Medical disclaimer
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.setCursor(10, 230); tft.println("Note: This is not accurate medical");
  tft.setCursor(10, 240); tft.println("diagnosis. Consult a doctor for");
  tft.setCursor(10, 250); tft.println("serious arrhythmia concerns.");
}

void startArrhythmiaWaveform() {
  onInitializing = false;
  onECGWaveform = true;
  
  tft.fillScreen(TFT_BLACK);
  
  baseline = tft.height() / 2;
  drawGrid();
  prevY = baseline;
  xPos = 0;
  ecgStartTime = millis();
}

void showArrhythmiaResults() {
  onECGWaveform = false;
  onArrhythmiaAnalysis = true;
  
  tft.fillScreen(TFT_BLACK);
  
  tft.drawRect(backBtnX, backBtnY, backBtnW, backBtnH, TFT_CYAN);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(backBtnX + 8, backBtnY + 8);
  tft.println("<");
  
  drawRedHeart();
  
  bpmUpdateTime = millis();
  updateArrhythmiaDisplay();
}

void updateArrhythmiaDisplay() {
  tft.fillRect(0, 200, tft.width(), 120, TFT_BLACK);
  
  int bpm = heartMonitor.getBPM();
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  
  if (bpm > 0) {
    // Display BPM on same line
    tft.setCursor(80, 200);
    tft.print("BPM: ");
    tft.println(bpm);
    
    // Analyze and display arrhythmia classification
    String classification;
    uint16_t color;
    
    if (bpm < 60) {
      classification = "Bradycardia";
      color = TFT_YELLOW;
    } else if (bpm > 100) {
      classification = "Tachycardia";
      color = TFT_RED;
    } else {
      classification = "Normal Sinus";
      color = TFT_GREEN;
    }
    
    // Center the classification text
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(2);
    int textWidth = classification.length() * 12;
    int x = (SCREEN_WIDTH - textWidth) / 2;
    tft.setCursor(x, 230);
    tft.println(classification);
    
    // Add "Rhythm" on next line if Normal Sinus
    if (bpm >= 60 && bpm <= 100) {
      String rhythm = "Rhythm";
      textWidth = rhythm.length() * 12;
      x = (SCREEN_WIDTH - textWidth) / 2;
      tft.setCursor(x, 250);
      tft.println(rhythm);
    }
    
    Serial.print("Arrhythmia Analysis - BPM: ");
    Serial.print(bpm);
    Serial.print(", Classification: ");
    Serial.println(classification);
  } else {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    String analyzeText = "Analyzing...";
    int textWidth = analyzeText.length() * 12; // Approximate width for size 2
    int x = (SCREEN_WIDTH - textWidth) / 2;
    tft.setCursor(x, 230);
    tft.println(analyzeText);
  }
}