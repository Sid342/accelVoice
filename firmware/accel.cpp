#include "accel.h"
#include "app_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>

static MPU6050        s_mpu(0x68);
static volatile bool  s_motion_flag = false;
static int16_t        s_off_x = 0, s_off_y = 0, s_off_z = 0;

static void IRAM_ATTR motion_isr(void) { s_motion_flag = true; }

static void i2c_bus_scan(void)
{
    Serial.print(F("[accel] I2C scan:"));
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F(" 0x"));
            if (addr < 16) Serial.print('0');
            Serial.print(addr, HEX);
            found++;
        }
    }
    if (!found) Serial.print(F(" (none)"));
    Serial.println();
}

bool accel_init(void)
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000UL);

    i2c_bus_scan();

    s_mpu.initialize();
    if (!s_mpu.testConnection()) {
        Serial.println(F("[accel] 0x68 no ack — retry 0x69"));
        s_mpu = MPU6050(0x69);
        s_mpu.initialize();
        if (!s_mpu.testConnection()) return false;
    }

    s_mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    s_mpu.setMotionDetectionThreshold(MPU_MOT_THR_REG);
    s_mpu.setMotionDetectionDuration(MPU_MOT_DUR_REG);
    s_mpu.setIntMotionEnabled(true);
    s_mpu.setIntDataReadyEnabled(false);
    s_mpu.setInterruptMode(0);
    s_mpu.setInterruptDrive(0);
    s_mpu.setInterruptLatch(1);
    s_mpu.setInterruptLatchClear(1);

    pinMode(PIN_MPU_INT, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), motion_isr, RISING);

    (void)s_mpu.getIntStatus();
    s_motion_flag = false;
    return true;
}

void accel_read_mg_raw(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg)
{
    int16_t ax, ay, az;
    s_mpu.getAcceleration(&ax, &ay, &az);
    *x_mg = (int16_t)((int32_t)ax * 125 / 2048);
    *y_mg = (int16_t)((int32_t)ay * 125 / 2048);
    *z_mg = (int16_t)((int32_t)az * 125 / 2048);
}

void accel_read_mg(int16_t *x_mg, int16_t *y_mg, int16_t *z_mg)
{
    int16_t rx, ry, rz;
    accel_read_mg_raw(&rx, &ry, &rz);
    *x_mg = rx - s_off_x;
    *y_mg = ry - s_off_y;
    *z_mg = rz - s_off_z;
}

bool accel_motion_flag_take(void)
{
    noInterrupts();
    bool f = s_motion_flag;
    s_motion_flag = false;
    interrupts();
    if (f) {
        (void)s_mpu.getIntStatus();
    }
    return f;
}

void accel_set_motion_thresh_mg(uint16_t mg)
{
    if (mg > 510) mg = 510;        /* MPU6050 reg is 8-bit, 2 mg/LSB → max 510 mg */
    uint8_t reg = (uint8_t)(mg / 2);
    if (reg < 1) reg = 1;
    s_mpu.setMotionDetectionThreshold(reg);
}

void accel_set_offsets_mg(int16_t ox, int16_t oy, int16_t oz)
{
    s_off_x = ox;
    s_off_y = oy;
    s_off_z = oz;
}

void accel_get_offsets_mg(int16_t *ox, int16_t *oy, int16_t *oz)
{
    *ox = s_off_x;
    *oy = s_off_y;
    *oz = s_off_z;
}

void accel_calibrate(void)
{
    /* Average 1 s of raw reads at ~50 Hz → 50 samples.
     * Target: x≈0, y≈0, z≈+1000 mg when flat.                           */
    int32_t sx = 0, sy = 0, sz = 0;
    const uint8_t N = 50;
    for (uint8_t i = 0; i < N; i++) {
        int16_t rx, ry, rz;
        accel_read_mg_raw(&rx, &ry, &rz);
        sx += rx; sy += ry; sz += rz;
        delay(20);
    }
    s_off_x = (int16_t)(sx / N);
    s_off_y = (int16_t)(sy / N);
    s_off_z = (int16_t)(sz / N) - 1000;   /* keep gravity */
}
