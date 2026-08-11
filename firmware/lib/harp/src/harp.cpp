#include "harp.h"


harp::harp(){}  


#if CAP_CHIP==1
  void harp::setup(){
    touch_sensor.begin();
    if (!touch_sensor.communicating()){
      Serial.println("Harp not communicating");
      return;
    }
  }

  void harp::recalibrate(){
      Serial.println("reseting...");
    touch_sensor.reset();
    delay(1000);
    Serial.println("triggerCalibration");
    touch_sensor.triggerCalibration();
    delay(50);
    int cal_timeout = 50;
    while (touch_sensor.calibrating() && cal_timeout > 0)
    {
      Serial.println("calibrating...");
      delay(50);
      cal_timeout--;
    }
    Serial.println("finished calibrating"); 
    touch_sensor.setMeasurementIntervalCount(1);
    touch_sensor.setDetectionIntegrator(2); 
    touch_sensor.setTowardsDriftCompensationDuration(26);
    touch_sensor.setAwayDriftCompensationDuration(8);
    touch_sensor.setRecalibrationDelay(0);
    AT42QT2120::KeyPulseScale key_pulse_scale;
    uint8_t pulse = 0x0;
    uint8_t scale = 0x0;
    key_pulse_scale.pulse = pulse;
    key_pulse_scale.scale = scale;
    for (uint8_t key=0; key < touch_sensor.KEY_COUNT; ++key){
      touch_sensor.setKeyDetectThreshold(key,threshold);
      touch_sensor.setKeyPulseScale(key,key_pulse_scale);
    }
  }


  void harp::update(debouncer (&data_array)[12]){
      float dummy[12];
      update(data_array, dummy);
  }

  void harp::update(debouncer (&data_array)[12], float (&pressure_array)[12]){
      AT42QT2120::Status status = touch_sensor.getStatus();
      uint8_t key_count= touch_sensor.KEY_COUNT;
      for (uint8_t key=0; key < key_count; ++key){
          uint8_t pad = remap_array[key];
          bool touched = touch_sensor.touched(status, key);
          data_array[pad].set(touched);
          pressure_array[pad] = touched ? 0.8f : 0.0f;
      }
  }
#else
  void harp::setup(){
    touch_sensor.setupSingleDevice(Wire,MPR121::ADDRESS_5A,true);
    touch_sensor.startAllChannels();
    if (!touch_sensor.communicating(MPR121::ADDRESS_5A)){
      Serial.println("Harp not communicating");
      return;
    }
  }

  void harp::recalibrate(){
    Serial.println("Recalibrating Harp");
    touch_sensor.setAllChannelsThresholds(touch_threshold,
    release_threshold);
    touch_sensor.setDebounce(MPR121::ADDRESS_5A,
    touch_debounce,
    release_debounce);
    touch_sensor.setBaselineTracking(MPR121::ADDRESS_5A,
    baseline_tracking);
    touch_sensor.setChargeDischargeCurrent(MPR121::ADDRESS_5A,
    charge_discharge_current);
    touch_sensor.setChargeDischargeTime(MPR121::ADDRESS_5A,
    charge_discharge_time);
    touch_sensor.setFirstFilterIterations(MPR121::ADDRESS_5A,
    first_filter_iterations);
    touch_sensor.setSecondFilterIterations(MPR121::ADDRESS_5A,
    second_filter_iterations);
    touch_sensor.setSamplePeriod(MPR121::ADDRESS_5A,
    sample_period);
    touch_sensor.startAllChannels();

  }


  void harp::update(debouncer (&data_array)[12]){
      float dummy[12];
      update(data_array, dummy);
  }

  void harp::update(debouncer (&data_array)[12], float (&pressure_array)[12]){
      uint16_t touch_status = touch_sensor.getTouchStatus(MPR121::ADDRESS_5A);
      if (touch_sensor.overCurrentDetected(touch_status)){
        Serial.println("Over current detected!\n\n");
        touch_sensor.startAllChannels();
        return;
      }
      for (uint8_t key=0; key < 12; key++){
          uint8_t pad = remap_array[key];
          bool touched = touch_sensor.deviceChannelTouched(touch_status, key);
          data_array[pad].set(touched);
          
          if (touched) {
            uint16_t baseline = touch_sensor.getDeviceChannelBaselineData(MPR121::ADDRESS_5A, key);
            uint16_t filtered = touch_sensor.getDeviceChannelFilteredData(MPR121::ADDRESS_5A, key);
            float target = 0.5f;
            if (baseline > filtered) {
              int16_t diff = baseline - filtered;
              target = static_cast<float>(diff - 30) / 160.0f;
              if (target < 0.1f) target = 0.1f;
              if (target > 1.0f) target = 1.0f;
            }
            // Low-pass filter to prevent jumpy readings on small touch pads
            smoothed_pressure_[pad] += 0.15f * (target - smoothed_pressure_[pad]);
            pressure_array[pad] = smoothed_pressure_[pad];
          } else {
            smoothed_pressure_[pad] = 0.0f;
            pressure_array[pad] = 0.0f;
          }
      }
  }
#endif
