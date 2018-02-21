#include <SPI.h>

SPISettings spiSettings(2e6, MSBFIRST, SPI_MODE3); // 2 MHz, mode 3

// Register Map for the ADNS3080 Optical OpticalFlow Sensor
#define ADNS3080_PRODUCT_ID            0x00
#define ADNS3080_MOTION                0x02
#define ADNS3080_DELTA_X               0x03
#define ADNS3080_DELTA_Y               0x04
#define ADNS3080_SQUAL                 0x05
#define ADNS3080_CONFIGURATION_BITS    0x0A
#define ADNS3080_MOTION_CLEAR          0x12
#define ADNS3080_FRAME_CAPTURE         0x13
#define ADNS3080_MOTION_BURST          0x50

// ADNS3080 hardware config
#define ADNS3080_PIXELS_X              30
#define ADNS3080_PIXELS_Y              30

// Id returned by ADNS3080_PRODUCT_ID register
#define ADNS3080_PRODUCT_ID_VALUE      0x17

static const uint8_t RESET_PIN = 9;
static const uint8_t SS_PIN = SS; // Pin 10

static float x_cum, y_cum;

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial port to open

  SPI.begin();

  // Set SS and reset pin as output
  pinMode(SS_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  reset();

  uint8_t id = spiRead(ADNS3080_PRODUCT_ID);
  if (id == ADNS3080_PRODUCT_ID_VALUE)
    Serial.println(F("ADNS-3080 found"));
  else {
    Serial.print(F("Could not find ADNS-3080: "));
    Serial.println(id, HEX);
    while (1);
  }

  uint8_t config = spiRead(ADNS3080_CONFIGURATION_BITS);
  spiWrite(ADNS3080_CONFIGURATION_BITS, config | 0x10); // Set resolution to 1600 counts per inch
}

void loop() {
  updateSensor();
  
  printPixelData();
  
  Serial.flush();
  
  reset();
  
  delay(150);
}

void printPixelData(void) {
  bool isFirstPixel = true;

  // Write to frame capture register to force capture of frame
  spiWrite(ADNS3080_FRAME_CAPTURE, 0x83);

  // Wait 3 frame periods + 10 nanoseconds for frame to be captured
  delayMicroseconds(1510); // Minimum frame speed is 2000 frames/second so 1 frame = 500 nano seconds. So 500 x 3 + 10 = 1510

  // Display the pixel data
  Serial.print(F("image"));
  for (uint8_t i = 0; i < ADNS3080_PIXELS_Y; i++) {
    for (uint8_t j = 0; j < ADNS3080_PIXELS_X; j++) {
      uint8_t regValue = spiRead(ADNS3080_FRAME_CAPTURE);
      if (isFirstPixel && !(regValue & 0x40)) {
        Serial.println(F("Failed to find first pixel"));
        goto reset;
      }
      isFirstPixel = false;
      uint8_t pixelValue = regValue << 2; // Only lower 6 bits have data
      Serial.write(pixelValue);
//      if (j != ADNS3080_PIXELS_X - 1)
//        Serial.write(',');
    }
//    Serial.println();
    Serial.flush();
  }
  Serial.println(F("end"));

reset:
  reset(); // Hardware reset to restore sensor to normal operation
}

void updateSensor(void) {
  // Read sensor
  int i;
  int32_t x = 0;
  int32_t y = 0;
  uint8_t quality = 0;
  uint8_t points = 0;
  float x_out;
  float y_out;
  
  /* Multiple captures in case fast motion is clipping at 8-bit. */
  
  for (i=0; i<10; i++)
  {
    uint8_t buf[4];
    spiRead(ADNS3080_MOTION_BURST, buf, 4);
    uint8_t motion = buf[0];
    //Serial.print(motion & 0x01); // Resolution

    if (motion & 0x10) // Check if we've had an overflow
      Serial.println(F("ADNS-3080 overflow\n"));
    else if (motion & 0x80)
    {
      int8_t dx = buf[1];
      int8_t dy = buf[2];
      uint8_t surfaceQuality = buf[3];

      x += dx;
      y += dy;
      quality += surfaceQuality;
      points++;
    }
#if 0
    else
      Serial.print(motion, HEX);
#endif
    
    delay(10);
  }
  
//  if ( abs(x) <= 1 ) x = 0;
  if ( x <= 1 ) x = 0;
  if ( abs(y) <= 1 ) y = 0;
  
//  if ( points > 0 )
  {
    x_cum = x_cum * 0.9 + abs(x) * 0.1;
    y_cum = y_cum * 0.9 + abs(x) * 0.1;
  }
  
//  if ( abs(x_cum) <= 1.0 ) x_cum = 0.0;
//  if ( abs(y_cum) <= 1.0 ) y_cum = 0.0;
  
//  x_out = abs( x_cum ) - 1.0;
//  y_out = abs( y_cum ) - 1.0;
  x_out = abs( x_cum );
  y_out = abs( y_cum );
  
  if ( x_out < 0.0 ) x_out = 0.0;
  if ( y_out < 0.0 ) y_out = 0.0;
  
  // Print values
  Serial.print(F("motion"));
  Serial.print(x);
  Serial.write(',');
  Serial.print(y);
  Serial.write(',');
  Serial.print( points ? ( quality / points ) : 0 );
  Serial.write(',');
//  Serial.print( sqrt(x*x+y*y) );
  Serial.print( sqrt(x_out*x_out+y_out*y_out) );
//  Serial.print( x_out );
//  Serial.print( sqrt(x_cum*x_cum+y_cum*y_cum) );
  Serial.print("");
  Serial.flush();
  Serial.println(F("end"));
}

void reset(void) {
  digitalWrite(RESET_PIN, HIGH); // Set high
  delayMicroseconds(10);
  digitalWrite(RESET_PIN, LOW); // Set low
  delayMicroseconds(500); // Wait for sensor to get ready
}

// Will cause the Delta_X, Delta_Y, and internal motion registers to be cleared
void clearMotion() {
  spiWrite(ADNS3080_MOTION_CLEAR, 0xFF); // Writing anything to this register will clear the sensor's motion registers
  x_cum = y_cum = 0.0;
}

void spiWrite(uint8_t reg, uint8_t data) {
  spiWrite(reg, &data, 1);
}

void spiWrite(uint8_t reg, uint8_t *data, uint8_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(SS_PIN, LOW);

  SPI.transfer(reg | 0x80); // Indicate write operation
  delayMicroseconds(75); // Wait minimum 75 us in case writing to Motion or Motion_Burst registers
  SPI.transfer(data, length); // Write data

  digitalWrite(SS_PIN, HIGH);
  SPI.endTransaction();
}

uint8_t spiRead(uint8_t reg) {
  uint8_t buf;
  spiRead(reg, &buf, 1);
  return buf;
}

void spiRead(uint8_t reg, uint8_t *data, uint8_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(SS_PIN, LOW);

  SPI.transfer(reg); // Send register address
  delayMicroseconds(75); // Wait minimum 75 us in case writing to Motion or Motion_Burst registers
  memset(data, 0, length); // Make sure data buffer is 0
  SPI.transfer(data, length); // Write data

  digitalWrite(SS_PIN, HIGH);
  SPI.endTransaction();
}

