
#define ENA 5
#define IN1 7
#define IN2 8

#define ENB 6
#define IN3 9
#define IN4 10

// HC-SR04 ultrasonic sensor pins
#define TRIG 11
#define ECHO 12

const int baseSpeed = 220;
const int turnSpeed = 220;

bool automaticMode = false;  // Starts in manual mode

void setup() {
  bool automaticMode = true;  // Starts in manual mode
  Serial.begin(9600);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  stopMotors();
}

void loop() {
  // Receive commands from ESP
  while (Serial.available() > 0) {
    char command = Serial.read();

    if (command == 'a' || command == 'A') {
      automaticMode = true;
      stopMotors();
      Serial.println("Automatic mode ON");
    }
    else if (command == 'm' || command == 'M') {
      automaticMode = false;
      stopMotors();
      Serial.println("Manual mode ON");
    }
    else if (!automaticMode) {
      // Manual control
      if (command == 'f' || command == 'F')
        forward(baseSpeed);

      else if (command == 'b' || command == 'B')
        backward(baseSpeed);

      else if (command == 'l' || command == 'L')
        left(baseSpeed);

      else if (command == 'r' || command == 'R')
        right(baseSpeed);

      else if (command == 's' || command == 'S')
        stopMotors();
    }
  }

  // Automatic obstacle avoidance
  if (automaticMode) {
    automaticControl();
  }
}

// Read ultrasonic distance in centimeters
float getDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  // Timeout prevents the code from waiting indefinitely
  unsigned long duration = pulseIn(ECHO, HIGH, 25000UL);

  if (duration == 0) {
    return -1;  // Invalid reading
  }

  return duration * 0.0343 / 2.0;
}

void automaticControl() {
  float distance = getDistance();


  if (distance < 0) {
    stopMotors();
    return;
  }

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  if (distance < 20) {
    // Obstacle too close: rotate right
    right(turnSpeed);
  }
  else {
    // Obstacle is at least 20 cm away:
    // move forward, slowing down between 20 and 30 cm
    int motorSpeed;

    if (distance < 30) {
      motorSpeed = map((int)(distance * 10),
                       200, 300, 100, baseSpeed);

      motorSpeed = constrain(motorSpeed, 100, baseSpeed);
    }
    else {
      motorSpeed = baseSpeed;
    }

    forward(motorSpeed);
  }

  delay(30);
}

void forward(int motorSpeed) {
  analogWrite(ENA, motorSpeed);
  analogWrite(ENB, motorSpeed);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void backward(int motorSpeed) {
  analogWrite(ENA, motorSpeed);
  analogWrite(ENB, motorSpeed);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void left(int motorSpeed) {
  analogWrite(ENA, motorSpeed);
  analogWrite(ENB, motorSpeed);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);

  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
}

void right(int motorSpeed) {
  analogWrite(ENA, motorSpeed);
  analogWrite(ENB, motorSpeed);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
}

void stopMotors() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);

  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}


