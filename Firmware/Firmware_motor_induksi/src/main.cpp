#include <Arduino.h>
#include <monitoring_sensor.cpp>
#include <pembacaan_freq.cpp>

// put function declarations here:
int myFunction(int, int);

void setup() {
  setup_pin();
  // put your setup code here, to run once:
  int result = myFunction(2, 3);
}

void loop() {
  loop_data();
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}