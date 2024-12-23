#include <mcp_can.h>
#include <SPI.h>
#include <TimerOne.h>

long unsigned int rxId;
unsigned char len = 0;
unsigned char rxBuf[8];
char msgString[128]; // Array to store serial string

uint16_t testID = 0x219;
uint32_t playground = 1;
unsigned char testBuf[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint16_t lastToggle = 0;
volatile bool timeIsUp = false;
char fmtBuf[50];
#define SEND_FREQ 100

uint32_t lastSent = 0;

#define CAN0_INT 2 // Set INT to pin 2
MCP_CAN CAN0(9);  // Set CS to pin 10

enum GearState {
  Off = 0,
  On = 1,
  BackL = 3,
  Blink = 4
};


struct ToggleState {
  GearState P;
  GearState R;
  GearState N;
  GearState D;
  GearState B;
};
ToggleState toggle;

/*
  Toggle positions:
  0000 Nothing
  6400 D1
  6000 D1
  8400 D2
  8000 D2
  2400 R1
  2000 R1
  4400 R2
  4000 R2
  0040 B
  0100 P
   */
const uint16_t MChange = 0x0400;
const uint16_t MDlittle = 0x6000;
const uint16_t MDfull = 0x8000;
const uint16_t MRlittle = 0x2000;
const uint16_t MRfull = 0x4000;
const uint16_t MB = 0x0040;
const uint16_t MP = 0x0100;

void sendPacket() 
{
  timeIsUp = true;
}

byte checksumm_0E6(const byte *frame, uint8_t len, uint8_t iter, uint8_t seed)
{

  byte cursumm = 0;
  byte checksum = 0;
  for (byte i = 0; i < len; i++)
  {
    cursumm += (frame[i] >> 4) + (frame[i] & 0x0F);
  }
  cursumm += iter;
  cursumm = ((cursumm ^ 0xFF) - seed) & 0x0F;
  checksum = (cursumm << 4) | iter ;
  return checksum;
}


void setup()
{
  Serial.begin(115200);

  // Initialize MCP2515 running at 16MHz with a baudrate of 500kb/s and the masks and filters disabled.

  while (CAN0.begin(MCP_ANY, CAN_500KBPS, MCP_16MHZ) != CAN_OK) {
    Serial.println("Error Initializing MCP2515...");
    delay(1000);
  }
  Serial.println("MCP2515 Initialized Successfully!");

  CAN0.setMode(MCP_NORMAL); // Set operation mode to normal so the MCP2515 sends acks to received data.

  pinMode(CAN0_INT, INPUT); // Configuring pin for /INT input

  Serial.println("Stellatnis Reverse Engineering Toggle");

  toggle.P = On;
  toggle.R = BackL;
  toggle.N = Off;
  toggle.D = BackL;
  toggle.B = Off;

  Timer1.initialize(SEND_FREQ * 1000ul);
  Timer1.attachInterrupt(sendPacket);
}

void loop()
{
  if (timeIsUp) {
    //uint8_t counterL = testBuf[0] & 0xF;

    uint32_t *bigNum = reinterpret_cast<uint32_t *>(testBuf + 1);
    *bigNum = 0;
    *bigNum |= (uint32_t)toggle.P << 1;
    *bigNum |= (uint32_t)toggle.R << 14;
    *bigNum |= (uint32_t)toggle.N << 11;
    *bigNum |= (uint32_t)toggle.D << 8;
    *bigNum |= (uint32_t)toggle.B << 21;

    testBuf[0] = 0; // erase checksum value for calculating checksum
    static uint8_t iter = 0;
    testBuf[0] = checksumm_0E6(testBuf, sizeof(testBuf), iter, 12);
    iter++;
    iter &= 0x0f;
    
    CAN0.sendMsgBuf(testID, sizeof(testBuf), testBuf);
    timeIsUp = false;
  }
  if (!digitalRead(CAN0_INT)) // If CAN0_INT pin is low, read receive buffer
  {
    CAN0.readMsgBuf(&rxId, &len, rxBuf); // Read data: len = data length, buf = data byte(s)

    // filter out known IDs
    if (rxId == 0x259) {
      // check for successful receival
      if (len == 3) {
        byte calcSum = checksumm_0E6(rxBuf + 1, len - 1, rxBuf[0] & 0x0f, 0);
        if (calcSum == rxBuf[0]) {
          // all good!
          // check what changed
          uint16_t currToggle = (rxBuf[1] << 8) | rxBuf[2];


  

          if (lastToggle != currToggle) {
              // something changed
                uint16_t leverPos = currToggle & 0xF000;
                switch(leverPos) {
                  case MDfull:
                    toggle.P = Off;
                    toggle.R = BackL;
                    toggle.N = BackL;
                    toggle.D = On;
                    toggle.B = BackL;
                    Serial.println("Gear D");
                    break;
                  case MRfull:
                    toggle.P = Off;
                    toggle.R = On;
                    toggle.N = BackL;
                    toggle.D = BackL;
                    toggle.B = Off;
                    Serial.println("Gear R");
                  case MRlittle:
                  case MDlittle:
                    if (((toggle.R == On) || (toggle.D == On)) && ((currToggle & MChange) == 0)) {
                      // switch back to neutral
                      toggle.P = BackL;
                      toggle.R = BackL;
                      toggle.N = On;
                      toggle.D = BackL;
                      toggle.B = Off;
                      Serial.println("Gear N");
                    }
                    break;
                }
             
                // check for P or B
                if ((currToggle & MP) && (toggle.N == On)) {
                  toggle.P = On;
                  toggle.R = BackL;
                  toggle.N = Off;
                  toggle.D = BackL;
                  toggle.B = Off;
                  Serial.println("Gear P");
                }
                if ((currToggle & MB) && (toggle.D == On)) {
                  if (toggle.B == On) {
                    toggle.B = BackL;
                    Serial.println("B off");
                  } else {
                    toggle.B = On;
                    Serial.println("B on");
                  }
                  
                }
          }
          // save for later
          lastToggle = currToggle;
        } else {
          sprintf(msgString, "!M0 %02X %02X [%02X]", rxBuf[0], calcSum, rxBuf[0] - calcSum);
          Serial.println(msgString);
        }
      } else {
        Serial.println("Frame 0x259 length mismatch");
      }
      
    } else if (rxId == 0x50D) {

    } else {

      if ((rxId & 0x80000000) == 0x80000000) // Determine if ID is standard (11 bits) or extended (29 bits)
        sprintf(msgString, "Extended ID: 0x%.8lX  DLC: %1d  Data:", (rxId & 0x1FFFFFFF), len);
      else
        sprintf(msgString, "Standard ID: 0x%.3lX       DLC: %1d  Data:", rxId, len);

      Serial.print(msgString);

      if ((rxId & 0x40000000) == 0x40000000)
      { // Determine if message is a remote request frame.
        sprintf(msgString, " REMOTE REQUEST FRAME");
        Serial.print(msgString);
      }
      else
      {
        for (byte i = 0; i < len; i++)
        {
          sprintf(msgString, " 0x%.2X", rxBuf[i]);
          Serial.print(msgString);
        }
      }

      Serial.println();
    }
  }

  // commands
  if (Serial.available()) {
    char c = Serial.read();
    

    switch (c) {
      case 'r':
        testID = 0x219;
        break;
      case 'w':
        testID++;
        break;
      case 's':
        testID--;
        break;
      case '1':
        playground <<= 1;
        Serial.println(playground);
        break;
      case '2':
        playground = 1;
        Serial.println(playground);
        break;
    }

    sprintf(fmtBuf, "%03x [%02x | %02x %02x %02x %02x %02x %02x %02x]", testID, testBuf[0], testBuf[1], testBuf[2], testBuf[3], testBuf[4], testBuf[5], testBuf[6], testBuf[7]);
    Serial.println(fmtBuf);
    }
}