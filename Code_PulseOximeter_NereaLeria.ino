

#include <Wire.h>    //Library allows I2C communication
#include "MAX30105.h" // Library for signal acquisition
MAX30105 bioSensor;

#include <BLEDevice.h>    
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID    "13f7b509-a5e3-4469-9308-4b219e12c53b"
#define BPM_CHAR_UUID   "17c0efa6-c395-4294-b463-5c6cd560bf91"
#define SPO2_CHAR_UUID  "63438932-e944-4ba5-a56a-2deb646a5cd2"

BLECharacteristic *pBPMChar; 
BLECharacteristic *pSpO2Char; 
bool deviceConnected = false; 

class MyServerCallbacks : public BLEServerCallbacks{
  void onConnect(BLEServer* pServer){
    deviceConnected = true;
  }
  void onDisconnect(BLEServer* pServer){
    deviceConnected = false;
    BLEDevice::startAdvertising();
  }
};

static const int DC_sec = 3;
static const int Fs_int = 100; // Hz
static const int dc_N = DC_sec * Fs_int;
static uint32_t ir_dc_buf[dc_N]; 
static uint32_t red_dc_buf[dc_N]; 

// Constants for filters 
static const float Fs = 100.0f; 
static const float HP_cutoff = 0.5f; 
static const float LP_cutoff = 5.0f; 
static const float butter_q = (1.0f/sqrt(2.0f));

// Variables for identifying finger state
static bool fingerPresent = false; 
static bool prevFingerPresent = false; 
static unsigned long startTime = 0;
static bool spo2ResetRequest = false;

struct Biquad{
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; 
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0; 
  float process(float x){
    float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2; 
    x2 = x1; x1 = x; 
    y2 = y1; y1 = y; 
    return y; 
  }
  void reset(){x1 = x2 = y1 = y2 = 0;}
};

Biquad lowPass(float fc, float fs, float q){
  Biquad f; 
  float w0 = 2.0f*PI*(fc/fs); 
  float cos_w0 = cosf(w0);
  float sin_w0 = sinf(w0);  
  float alpha = sin_w0/(2.0f*q);  
  float b0 = (1-cos_w0)/2.0f; 
  float b1 = (1-cos_w0);
  float b2 = (1-cos_w0)/2.0f; 
  float a0 = 1 + alpha; 
  float a1 = -2*cos_w0; 
  float a2 = 1-alpha; 

  f.b0 = b0/a0; 
  f.b1 = b1/a0; 
  f.b2 = b2/a0; 
  f.a1 = a1/a0; 
  f.a2 = a2/a0; 
  return f;

}

Biquad highPass(float fc, float fs, float q){
  Biquad f; 
  float w0 = 2.0f*PI*(fc/fs); 
  float cos_w0 = cosf(w0);
  float sin_w0 = sinf(w0);  
  float alpha = sin_w0/(2.0f*q);  
  float b0 = (1+cos_w0)/2.0f; 
  float b1 = -(1+cos_w0);
  float b2 = (1+cos_w0)/2.0f; 
  float a0 = 1 + alpha; 
  float a1 = -2*cos_w0; 
  float a2 = 1-alpha; 

  f.b0 = b0/a0; 
  f.b1 = b1/a0; 
  f.b2 = b2/a0; 
  f.a1 = a1/a0; 
  f.a2 = a2/a0; 
  return f;

}

float bandpass(float x, Biquad &hp, Biquad &lp){
  return lp.process(hp.process(x));
}

static Biquad ir_HP, ir_LP, red_HP, red_LP;
static float ir_ac_filt = 0, red_ac_filt = 0;
static float ir_dc_filt = 0, red_dc_filt = 0;


 // Adaptive threshold behaviour for peak detection 
static float ampEMA = 0; 
static const float ampAlpha = 0.05f; 
static int32_t prev_prev = 0;  // Corresponds to sample n-2
static int32_t prev = 0;       // Corresponds to sample n-1
static bool armed = true;

// PROTOTYPES
void setupBLE();
void resetFilters();
bool checkFingerPresence(uint32_t ir_raw);
bool detectionPeak(float ir_ac_filt);
int calculationBPM(bool peakDetected, bool fingerPresent);
int calculationSpO2(uint32_t ir_raw, uint32_t red_raw, bool fingerPresent, bool peakDetected);


void setup() {
  
  Serial.begin(115200); 
  delay(1500);
  Wire.begin();
  setupBLE();


  while ( !bioSensor.begin(Wire)){
    Serial.println("MAX30102 not found.");
    delay(1000);
  }

  const byte LED_Brightness = 70;  // Range between 0 - 255 
  const byte LED_Mode = 2;  // RED and IR lights both active
  const byte sample_Average = 4; 
  const int sample_Rate = 100;   
  const int pulse_Width = 411;  
  const int ADC_Range = 16384; 

  bioSensor.setup(LED_Brightness, sample_Average, LED_Mode, sample_Rate, pulse_Width, ADC_Range);
 
  ir_HP = highPass(HP_cutoff, Fs, butter_q); 
  ir_LP = lowPass(LP_cutoff, Fs, butter_q); 
  red_HP = highPass(HP_cutoff, Fs, butter_q); 
  red_LP = lowPass(LP_cutoff, Fs, butter_q); 

  startTime = millis();  // Initializes the start time just once
  resetFilters();

}
 
void loop() {

  bioSensor.check();
  while(bioSensor.available()){
    
    uint32_t red_raw = bioSensor.getFIFORed();
    uint32_t ir_raw = bioSensor.getFIFOIR();
      
    fingerPresent = checkFingerPresence(ir_raw);
    static bool lastFinger = false; 
    if(fingerPresent && !lastFinger){
      spo2ResetRequest = true;
    }
    lastFinger = fingerPresent;

    // DC removal 
    static float ir_dc = 0, red_dc = 0; 
    static const float dcAlpha = 0.02f; 
    if(ir_dc == 0) ir_dc = (float)ir_raw; else ir_dc += dcAlpha*((float)ir_raw - ir_dc);
    if(red_dc == 0) red_dc = (float)red_raw; else red_dc += dcAlpha*((float)red_raw - red_dc);

    ir_dc_filt = ir_dc; 
    red_dc_filt = red_dc; 

    // AC component 
    float ir_ac =(float)ir_raw - ir_dc;
    float red_ac =(float)red_raw - red_dc;
    ir_ac_filt = bandpass(ir_ac, ir_HP, ir_LP);
    red_ac_filt = bandpass(red_ac, red_HP, red_LP);

    bool peakDetected = detectionPeak(ir_ac_filt);

    // BPM and SpO2
    int BPM = 0; 
    int SpO2 = -1; 

    if(fingerPresent){
      BPM = calculationBPM(peakDetected, fingerPresent); 
      SpO2 = calculationSpO2(ir_ac_filt, red_ac_filt, ir_dc_filt, red_dc_filt, fingerPresent, peakDetected);
    }else{
      SpO2 = -1; 
      BPM = 0;
    }
    Serial.print(" BPM:");Serial.print(BPM);
    Serial.print(" SPO2: "); Serial.println(SpO2);

    // BLE notify
    static unsigned long lastNotify = 0;
    const unsigned long notify_ms = 250;
    unsigned long now = millis();

    if(prevFingerPresent && !fingerPresent && deviceConnected){
      pBPMChar->setValue("0");  pBPMChar->notify();
      pSpO2Char->setValue("--"); pSpO2Char->notify();
      lastNotify = now;
    }

    if(deviceConnected && (now - lastNotify) > notify_ms ){
      lastNotify = now;

      char BPM_string[8];
      char SpO2_string[8];

      snprintf(BPM_string, sizeof(BPM_string), "%d", BPM);
      pBPMChar->setValue(BPM_string);
      pBPMChar->notify();

      if(SpO2 >= 0) snprintf(SpO2_string, sizeof(SpO2_string), "%d", SpO2);
      else          snprintf(SpO2_string, sizeof(SpO2_string), "--");

      pSpO2Char->setValue(SpO2_string);
      pSpO2Char->notify();
    }

    prevFingerPresent = fingerPresent;
    bioSensor.nextSample();  // Sample finished, move to the next sample
  }

}

void setupBLE(){
   BLEDevice::init("Pulse Oximeter ESP32");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pBPMChar = pService->createCharacteristic(
    BPM_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pBPMChar->addDescriptor(new BLE2902());

  pSpO2Char = pService->createCharacteristic(
    SPO2_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pSpO2Char->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(true);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

void resetFilters(){
 
  ampEMA = 0; 
  armed = true; 
  prev = 0; 
  prev_prev = 0;
  ir_HP.reset();
  ir_LP.reset();
  red_HP.reset();
  red_LP.reset();
}

bool checkFingerPresence(uint32_t raw_signal){
  static bool fingerStable = false; 
  static bool fingerCandidate = false;
  static unsigned long fingerChange = 0; 
  const unsigned long fingerDebounce_ms = 250;

  bool candidate = fingerStable ? (raw_signal > 40000) : (raw_signal > 35000);
  // This value obtained from observing the values of raw_signal while the finger was pressed on the sensor
  if (candidate != fingerStable){
    if (candidate != fingerCandidate){
      fingerCandidate = candidate; 
      fingerChange = millis();
    }
    if (millis() - fingerChange >= fingerDebounce_ms){
      fingerStable = candidate; 
      resetFilters();
      startTime = millis();
    }
  }else{
    fingerCandidate = candidate;
  }
  return fingerStable;
}

bool detectionPeak(float ir_ac_filt){
  static float ir_dc_peak = 0.0f; // Local DC component
  const float dcAlpha = 0.02f;
  if(ir_dc_peak == 0.0f) ir_dc_peak = ir_ac_filt; 
  ir_dc_peak += dcAlpha *(ir_ac_filt- ir_dc_peak);
  float ir_ac_peak = ir_ac_filt- ir_dc_peak;

  ir_ac_peak *= 2.0f;
  int32_t ir_ac_peak_int = (int32_t)ir_ac_peak;
  ir_ac_peak_int = -ir_ac_peak_int;

    ampEMA = (1.0f - ampAlpha) * ampEMA + ampAlpha * (float)abs(ir_ac_peak_int);
    int32_t threshold_High = (int32_t)max(8.0f, 0.25f * ampEMA);
    int32_t threshold_Low = (int32_t)max(3.0f, 0.15f * ampEMA);

  bool peakDetected = false;
  if (abs(ir_ac_peak_filt) < threshold_Low) armed = true;
    
    // Detect if "prev" was a peak only if armed
    if (armed){
      if ((prev > prev_prev) && (prev > ir_ac_peak_int) && (prev > threshold_High)){
        peakDetected = true;
        armed = false; // block until signal goes low again

      }
    }
    prev_prev = prev; 
    prev = ir_ac_peak_int; 
    return peakDetected;
}


int calculationBPM(bool peakDetected, bool fingerPresent){
  const int N = 5;  // 5 beats to obtain average
  const unsigned long setup_ms = 1500; 
  const unsigned long refract_ms = 500; 
  static unsigned long IBI_buffer[N] = {0};
  static int IBI_i = 0; 
  static int IBI_count = 0;
  static unsigned long lastBeatTime = 0; 
  static int BPM = 0;
  unsigned long now = millis();
  static float BPM_ema = 0; 
  static float beta = 0.2f;
  static unsigned long lastGoodIBI = 0; 
  
  // If no finger then reset and setup time after finger is placed 
  if(!fingerPresent || (now - startTime < setup_ms)){
    lastBeatTime = 0; 
    BPM = 0; 
    IBI_count = 0;
    BPM_ema = 0;
    lastGoodIBI = 0;
    return 0;
  }
  
  if(!peakDetected) return BPM; // If no peak has been detected it return the last BPM 

  // Refractory period, it ignores peaks too close to each other
  if (lastBeatTime != 0 && (now - lastBeatTime) < refract_ms)return BPM; 

  // First valid beat
  if (lastBeatTime == 0){
    lastBeatTime = now; 
    return BPM;
  }

  unsigned long IBI = now - lastBeatTime; // Interbeat Interval, is the exact time period between consecutive heartbeats
  lastBeatTime = now;

  if(IBI < 400 || IBI > 1500){ // Limits possible values 
    lastBeatTime = now;
    return BPM; 
  }
  // This ignores outlier IBI values that occur because of missed or false peaks due to motion or noise.
  // It prevents pulling BPM down caused by a single bad interval
  if (lastGoodIBI != 0){
    // Applied an acceptance band that allows changes from one beat to the next
    if (IBI > (unsigned long)(1.25f * lastGoodIBI) || IBI < (unsigned long)(0.75f * lastGoodIBI)){
      lastBeatTime = now;
      return BPM;
    }
  }
  // Valid heart beat
  lastBeatTime = now;
  lastGoodIBI = IBI;

  IBI_buffer[IBI_i] = IBI; 
  IBI_i = (IBI_i +1) % N; 
  if(IBI_count < N) IBI_count++;

  // Necessary to calculate average IBI
  if(IBI_count >= 2){
    unsigned long sum = 0;
    for(int i = 0; i < IBI_count; i++){
     sum += IBI_buffer[i];
    
    }
     unsigned long IBI_average = sum/IBI_count;
     int BPM_new = (int)(60000UL/IBI_average);
      if(BPM_ema == 0) BPM_ema = BPM_new;
      else BPM_ema = (1.0f - beta) * BPM_ema + beta *BPM_new;
    BPM = (int)(BPM_ema + 0.5f);
    
  }
  
  return BPM;
}


int calculationSpO2(float ir_ac_filt, float red_ac_filt, float ir_dc_filt, float red_dc_filt, bool fingerPresent, bool peakDetected) {
  
  static int lastSpO2 = -1;  
  static float SpO2_ema = 0;  
  
  static float ir_peak_buf[10] = {0}, red_peak_buf[10] = {0}; // Takes 10 samples
  static int buf_i = 0;
  
  ir_peak_buf[buf_i] = ir_ac_filt;
  red_peak_buf[buf_i] = red_ac_filt;
  buf_i = (buf_i + 1) % 10;

  float ir_max = -1000; 
  float ir_min = 1000; 
  float red_max = -1000; 
  float red_min = 1000;
  for(int i = 0; i < 10; i++) {
    if(ir_peak_buf[i] > ir_max) ir_max = ir_peak_buf[i];
    if(ir_peak_buf[i] < ir_min) ir_min = ir_peak_buf[i];
    if(red_peak_buf[i] > red_max) red_max = red_peak_buf[i];
    if(red_peak_buf[i] < red_min) red_min = red_peak_buf[i];
  }
  
  // Pulse amplitude 
  float IR_AC_ampl = (ir_max - ir_min);
  float RED_AC_ampl = (red_max - red_min);

  float IR_DC = ir_dc_filt;
  float RED_DC = red_dc_filt;

  if(fingerPresent && (millis() - startTime > 3000)) {
    if((IR_AC_ampl/IR_DC) > 0.002 && (RED_AC_ampl/RED_DC) > 0.001) {  
      float R = (RED_AC_ampl/RED_DC) / (IR_AC_ampl/IR_DC);
      int SpO2_new = (int)(116 - 25*R);
      if(SpO2_new >= 85 && SpO2_new <= 100) {
        lastSpO2 = SpO2_new;  
        if(SpO2_ema == 0) SpO2_ema = SpO2_new;
        else SpO2_ema = 0.1f * SpO2_new + (1-0.1f) * SpO2_ema;
        
        return (int)(SpO2_ema + 0.5f);

      }
    }
  }

  return lastSpO2;
}







