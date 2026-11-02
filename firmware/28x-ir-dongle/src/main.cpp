// Bluetooth IR adapter for Multimeter Viewer.
//
// Bridges the meter's optical (IR) serial port to the app: bytes the app
// sends go out through the IR LED, bytes the IR receiver picks up go back to
// the app. The meter protocol is not interpreted here; the app talks to the
// meter exactly as over the IR USB cable (115200 baud, 8N1).
//
// The app is reached over USB (CDC serial) while a computer or phone is
// plugged into the adapter's USB port, otherwise over Bluetooth LE. USB is
// only available on chips with their own USB controller (ESP32-C3, -S3,
// -C6); the classic ESP32 always uses Bluetooth.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <driver/uart.h>
#include <freertos/stream_buffer.h>
#include <soc/soc_caps.h>
#if __has_include(<driver/gpio_filter.h>)
#include <driver/gpio_filter.h>
#endif

// --- Configuration ---------------------------------------------------------

#ifndef IR_TX_PIN
#define IR_TX_PIN 17  // drives the IR LED through 220 Ω
#endif
#ifndef IR_RX_PIN
#define IR_RX_PIN 16  // IR phototransistor with pull-up
#endif

// Light means a 0 bit on the meter's IR port, darkness the idle level. With
// the LED switched on by a HIGH pin, the TX signal must be inverted; a
// phototransistor that pulls the pin LOW on light needs no inversion.
#ifndef IR_TX_INVERT
#define IR_TX_INVERT 1
#endif
#ifndef IR_RX_INVERT
#define IR_RX_INVERT 0
#endif

// Discards what the receiver picks up while the LED sends (its own light),
// and for IR_ECHO_SUPPRESS_US after the last bit: long enough for the tail
// of the echo, short enough to keep the start of the meter's answer.
#ifndef IR_ECHO_SUPPRESS
#define IR_ECHO_SUPPRESS 1
#endif
#ifndef IR_ECHO_SUPPRESS_US
#define IR_ECHO_SUPPRESS_US 200
#endif

// Ignores pulses on the receiver pin shorter than this (ns), e.g. 500;
// 0 = off. Uses the chip's flex glitch filter (ESP32-C6, -H2, -P4), up to
// 1600 ns. A bit at 115200 baud is 8680 ns, so stay well below that.
#ifndef IR_RX_GLITCH_NS
#define IR_RX_GLITCH_NS 0
#endif

// Optional LED that is on while the app is connected; -1 = none.
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN -1
#endif

// USB mode: needs the chip's USB Serial/JTAG controller as `Serial`
// (ARDUINO_USB_CDC_ON_BOOT=1 in platformio.ini).
#ifndef USB_BRIDGE
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
#define USB_BRIDGE 1
#else
#define USB_BRIDGE 0
#endif
#endif

static constexpr uint32_t kBaud = 115200;

// How long the USB host must be present or gone before switching.
static constexpr uint32_t kSwitchDebounceMs = 300;

// Must match lib/ble/ble_serial.dart (IrAdapterBle).
static const char *kServiceUuid = "ca7c33cd-93df-40a9-83c7-4c9e4ee57a33";
static const char *kToMeterUuid = "077fee1f-2aa0-4b7a-8adb-8c1f7c51310b";
static const char *kFromMeterUuid = "9612beb0-f6ef-41ee-9a61-d7d51add4703";

// Debug output only where `Serial` is not the USB link to the app.
#if USB_BRIDGE
#define LOG(...)
#else
#define LOG(...) Serial.printf(__VA_ARGS__)
#endif

// --- State -----------------------------------------------------------------

enum class Link { bluetooth, usb };

static HardwareSerial &ir = Serial1;
static NimBLEServer *server = nullptr;
static NimBLECharacteristic *fromMeter = nullptr;

// Bytes from the app over Bluetooth, filled by the Bluetooth task, sent in
// loop().
static StreamBufferHandle_t toMeterBuffer;

static volatile Link activeLink = Link::bluetooth;
static volatile bool bleConnected = false;
static volatile uint16_t connHandle = 0;

static void showConnected(bool on) {
  if (STATUS_LED_PIN >= 0) digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
}

// --- Bluetooth -------------------------------------------------------------

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    connHandle = info.getConnHandle();
    bleConnected = true;
    showConnected(true);
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    bleConnected = false;
    xStreamBufferReset(toMeterBuffer);
    if (activeLink == Link::bluetooth) {
      showConnected(false);
      NimBLEDevice::startAdvertising();
    }
  }
};

class ToMeterCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    NimBLEAttValue value = characteristic->getValue();
    xStreamBufferSend(toMeterBuffer, value.data(), value.length(), 0);
  }
};

// "MMV-IR-" plus the last two bytes of the Bluetooth address, so several
// adapters can be told apart.
static String adapterName() {
  uint64_t mac = ESP.getEfuseMac();
  char name[16];
  snprintf(name, sizeof(name), "MMV-IR-%02X%02X",
           static_cast<unsigned>((mac >> 32) & 0xFF),
           static_cast<unsigned>((mac >> 40) & 0xFF));
  return String(name);
}

static void setupBluetooth() {
  String name = adapterName();
  NimBLEDevice::init(name.c_str());
  NimBLEDevice::setMTU(247);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *service = server->createService(kServiceUuid);
  NimBLECharacteristic *toMeter = service->createCharacteristic(
      kToMeterUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  toMeter->setCallbacks(new ToMeterCallbacks());
  fromMeter =
      service->createCharacteristic(kFromMeterUuid, NIMBLE_PROPERTY::NOTIFY);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setName(name.c_str());
  advertising->enableScanResponse(true);
  advertising->start();
  LOG("Advertising as %s\n", name.c_str());
}

// --- USB / Bluetooth switching ---------------------------------------------

static bool usbHostPresent() {
#if USB_BRIDGE
  return HWCDC::isPlugged();
#else
  return false;
#endif
}

// USB while a host is plugged in, Bluetooth otherwise. Switching to USB
// ends a Bluetooth connection and stops advertising, so the app cannot use
// both at once.
static void updateLink() {
  static bool lastPresent = false;
  static uint32_t changedAt = 0;
  bool present = usbHostPresent();
  if (present != lastPresent) {
    lastPresent = present;
    changedAt = millis();
  }
  if (millis() - changedAt < kSwitchDebounceMs) return;

  if (present && activeLink == Link::bluetooth) {
    activeLink = Link::usb;
    if (bleConnected) server->disconnect(connHandle);
    NimBLEDevice::stopAdvertising();
    xStreamBufferReset(toMeterBuffer);
    showConnected(true);
  } else if (!present && activeLink == Link::usb) {
    activeLink = Link::bluetooth;
    showConnected(false);
    NimBLEDevice::startAdvertising();
  }
}

// --- IR --------------------------------------------------------------------

static void setupRxGlitchFilter() {
#if IR_RX_GLITCH_NS > 0
#if defined(SOC_GPIO_FLEX_GLITCH_FILTER_NUM) && SOC_GPIO_FLEX_GLITCH_FILTER_NUM > 0
  // The XTAL clock (40 MHz) allows the longest window: 64 cycles, 1600 ns.
  gpio_flex_glitch_filter_config_t config = {};
  config.clk_src = GLITCH_FILTER_CLK_SRC_XTAL;
  config.gpio_num = static_cast<gpio_num_t>(IR_RX_PIN);
  config.window_width_ns = IR_RX_GLITCH_NS;
  config.window_thres_ns = IR_RX_GLITCH_NS;
  gpio_glitch_filter_handle_t filter;
  if (gpio_new_flex_glitch_filter(&config, &filter) == ESP_OK &&
      gpio_glitch_filter_enable(filter) == ESP_OK) {
    LOG("RX glitch filter: %d ns\n", IR_RX_GLITCH_NS);
  } else {
    LOG("RX glitch filter: %d ns not possible\n", IR_RX_GLITCH_NS);
  }
#else
#warning "IR_RX_GLITCH_NS: this chip has no flex glitch filter; ignored"
#endif
#endif
}

static void setupIr() {
  ir.begin(kBaud, SERIAL_8N1, IR_RX_PIN, IR_TX_PIN);
  uint32_t inverse = UART_SIGNAL_INV_DISABLE;
  if (IR_TX_INVERT) inverse |= UART_SIGNAL_TXD_INV;
  if (IR_RX_INVERT) inverse |= UART_SIGNAL_RXD_INV;
  uart_set_line_inverse(UART_NUM_1, inverse);
  // Hand every received byte on at once instead of collecting them in the
  // UART's hardware FIFO first.
  ir.setRxFIFOFull(1);
  ir.setRxTimeout(1);
  setupRxGlitchFilter();
}

static void sendToMeter(const uint8_t *data, size_t length) {
  ir.write(data, length);
  ir.flush();  // waits until the last bit has left the LED
#if IR_ECHO_SUPPRESS
  delayMicroseconds(IR_ECHO_SUPPRESS_US);
  ir.flush(false);  // drops the echo, including what is still in the FIFO
#endif
}

// Sends what the app wrote, over the active link, through the IR LED.
// Returns whether there was anything to send.
static bool forwardToMeter() {
  uint8_t data[64];
  size_t length;
  bool sent = false;
#if USB_BRIDGE
  if (activeLink == Link::usb) {
    while (Serial.available() > 0) {
      length = Serial.read(data, sizeof(data));
      sendToMeter(data, length);
      sent = true;
    }
    return sent;
  }
#endif
  while ((length = xStreamBufferReceive(toMeterBuffer, data, sizeof(data), 0)) > 0) {
    sendToMeter(data, length);
    sent = true;
  }
  return sent;
}

// Passes on what the meter answered, in packets as large as the link
// allows. Waits briefly for more bytes so a line is not split into many
// tiny Bluetooth notifications. Returns whether anything arrived.
static bool forwardFromMeter() {
  if (!ir.available()) return false;
  size_t maxPacket = 244;
  if (activeLink == Link::bluetooth) {
    uint16_t mtu = bleConnected ? server->getPeerMTU(connHandle) : 23;
    if (mtu > 3 && mtu - 3u < maxPacket) maxPacket = mtu - 3;
  }
  uint8_t packet[244];

  size_t length = 0;
  uint32_t lastByte = micros();
  while (length < maxPacket) {
    if (ir.available()) {
      packet[length++] = ir.read();
      lastByte = micros();
    } else if (micros() - lastByte > 2000) {
      break;
    }
  }

#if USB_BRIDGE
  if (activeLink == Link::usb) {
    Serial.write(packet, length);
    return true;
  }
#endif
  if (bleConnected) {  // else nobody to tell
    fromMeter->setValue(packet, length);
    fromMeter->notify();
  }
  return true;
}

// --- Main ------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
#if USB_BRIDGE
  // Without an app reading, drop data instead of blocking the loop.
  Serial.setTxTimeoutMs(0);
#endif
  if (STATUS_LED_PIN >= 0) {
    pinMode(STATUS_LED_PIN, OUTPUT);
    showConnected(false);
  }
  toMeterBuffer = xStreamBufferCreate(1024, 1);
  setupIr();
  setupBluetooth();
}

void loop() {
  updateLink();
  bool busy = forwardToMeter();
  busy |= forwardFromMeter();
  if (!busy) delay(1);  // only rests while idle, so answers are not delayed
}
