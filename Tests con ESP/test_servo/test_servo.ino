#include <ESP32Servo.h>

Servo myServo;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  myServo.attach(18);
  Serial.println("Servo test readyyyyy");

}


void loop() {
  // put your main code here, to run repeatedly:

  int x = analogRead(34);
  int y = analogRead(35);

  int servoAngle = map(x, 0, 4095, 0, 180);
  myServo.write(servoAngle);
  Serial.println(servoAngle);
  delay(100);

}