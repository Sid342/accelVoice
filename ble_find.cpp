#include "ble_find.h"
#include "find.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/* Custom service + characteristic UUIDs (ad-hoc, atovio-themed). */
#define ATV_SVC_UUID  "4f76746f-0000-1000-8000-00805f9b34fb"
#define ATV_RING_UUID "4f76746f-0001-1000-8000-00805f9b34fb"

static BLEServer         *s_server = nullptr;
static BLEAdvertising    *s_adv    = nullptr;
static bool               s_inited = false;
static bool               s_advertising = false;
static uint32_t           s_until_ms    = 0;
#define DEFAULT_ADV_MS  300000UL   /* 5 minutes */

class RingCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        /* Any write triggers the LED strobe — same module as the web UI. */
        find_start(30000);
        Serial.println(F("[ble] ring write → strobing LED"));
    }
};

void ble_find_init(void)
{
    if (s_inited) return;
    BLEDevice::init("atovio-bench");

    s_server = BLEDevice::createServer();
    BLEService *svc = s_server->createService(ATV_SVC_UUID);

    BLECharacteristic *ring = svc->createCharacteristic(
        ATV_RING_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    ring->setCallbacks(new RingCb());
    ring->addDescriptor(new BLE2902());

    svc->start();

    s_adv = BLEDevice::getAdvertising();
    s_adv->addServiceUUID(ATV_SVC_UUID);
    s_adv->setScanResponse(true);
    s_adv->setMinPreferred(0x06);
    s_adv->setMinPreferred(0x12);

    s_inited = true;
    s_advertising = false;
    Serial.println(F("[ble] init done — not advertising"));
}

void ble_find_start_adv(uint32_t duration_ms)
{
    if (!s_inited) return;
    if (duration_ms == 0) duration_ms = DEFAULT_ADV_MS;
    s_adv->start();
    s_advertising = true;
    s_until_ms = millis() + duration_ms;
    Serial.printf("[ble] advertising — %lu ms\n", (unsigned long)duration_ms);
}

void ble_find_stop_adv(void)
{
    if (!s_inited) return;
    s_adv->stop();
    s_advertising = false;
    s_until_ms = 0;
    Serial.println(F("[ble] advertising stopped"));
}

bool ble_find_is_advertising(void) { return s_advertising; }

uint32_t ble_find_remaining_ms(void)
{
    if (!s_advertising) return 0;
    uint32_t now = millis();
    if ((int32_t)(now - s_until_ms) >= 0) return 0;
    return s_until_ms - now;
}

void ble_find_loop_tick(void)
{
    if (!s_advertising) return;
    if (millis() >= s_until_ms) {
        ble_find_stop_adv();
    }
}
