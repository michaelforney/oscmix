 /** 
 * libs used:
 * https://github.com/tttapa/Control-Surface
 * https://github.com/adafruit/Adafruit_TinyUSB_Arduino
 *
 * board: 
 * https://github.com/earlephilhower/arduino-pico
 * 
 * HowTo:
 * Connect an UART to USB Adapter to Pico UART0=Serial1 (Pin1=TX, Pin2=RX, Pin3=GND - from Pico's view)
 * Set your minicom (or whatever serial terminal) to 115200 8-N-1 and set it to show the Hex Packets.
 * Modify line 29 (Unit Name), 32 (Midi Port Name), 65 (Payload) according to your unit.
 * After connecting the pico via its Mini-USB Port to iPad via CamKit, open the TMFX app and press the bootsel button (LED gets on). Pico starts sending the sysex messages.
 * After a few seconds the app shows connected state and you should see the raw capture data fired out by the app in your serial terminal.
 * If you want to capture the outgoing data from pico too, enable the second pipe in line 35
 */

#include <Control_Surface.h> // Include the Control Surface library

// Instantiate the MIDI interface - Pico <-> iPad
USBMIDI_Interface midiusb;
// Instantiate the Serial interface - Pico UART0=Serial1 (Pin1=TX, Pin2=RX, Pin3=GND)
HardwareSerialMIDI_Interface midiser {Serial1, 115200};
// Create 3 MIDI pipes to connect the two interfaces midiusb and midiser (only one is used here)
MIDI_PipeFactory<3> pipes;

void setup() {
  TinyUSBDevice.setManufacturerDescriptor("RME"); //not really necessary
  TinyUSBDevice.setProductDescriptor("Fireface 802 (33443344)");
  //TinyUSBDevice.setProductDescriptor("Fireface UFX II (55665566)");
  //TinyUSBDevice.setProductDescriptor("Fireface UFX+ (77887788)");
  midiusb.backend.backend.setCableName(1, "Port 2");  // 802 and UFX+ use Port 2
  //midiusb.backend.backend.setCableName(1, "Port 3"); // UFX II uses Port 3
  midiusb >> pipes >> midiser; // all incoming midi from midiusb is sent to midiser Serial (UART0) 
  //Control_Surface >> pipes >> midiser; // all outgoing midi from pico is sent to Serial (UART0) - enable, if you want to capture outgoing messages 
   // Initializing all the stuff
  MIDI_Interface::beginAll();
  Control_Surface.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  Serial1.begin(115200);
}

void loop() {
  // Continuously handle MIDI input
  MIDI_Interface::updateAll();
  static bool ledState = LOW;
  //if Bootsel Button is pressed, lets start the magic to fake a Unit connection to App
  if (BOOTSEL) {
        ledState = !ledState; // Invert the state of the LED
        digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
        delay(1000);
   }
  if (ledState) { //call sysexInit Function (see below) every 50 ms
        sysexInit();
        delay(50);
  }
}

void sysexInit()
 {                                                      //  Setup System exclusive messages Arrays
    uint8_t chunk1[] = {0xF0};                          //  Sysex Start ----------------------------------
    uint8_t chunk2[] = {0x00, 0x20, 0x0D};              //  Manufacurer (RME uses MidiTemp)
    uint8_t chunk3[] = {0x10};                          //  Device ID
    uint8_t chunk4[] = {0x00};                          //  Sub ID
    uint8_t chunk5[] = {0x00, 0x12, 0x00, 0x78, 0x0B};  //  Payload for ff802
    //uint8_t chunk5[] = {0x00, 0x2E, 0x00, 0x10, 0x03};//  Payload for ufxii 
    //uint8_t chunk5[] = {0x12, 0x5C, 0x00, 0x10, 0x03};//  Payload for ufx+
    uint8_t chunk6[] = {0xF7};                          //  Sysex End ------------------------------------
    
                                                        //  Fire the chunks out
    midiusb.sendSysEx(chunk1);
    midiusb.sendSysEx(chunk2);
    midiusb.sendSysEx(chunk3);
    midiusb.sendSysEx(chunk4);
    midiusb.sendSysEx(chunk5);
    midiusb.sendSysEx(chunk6);
  }
