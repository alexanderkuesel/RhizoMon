#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <ArduinoBLE.h>

// WiFi credentials
const char* ssid = "Kuecha";
const char* password = "Almajo730";

// MQTT broker settings
const char* broker = "192.168.5.110";
int port = 1883;
const char* topic = "MUTHUR/NDATA/BASE/NCL";
const char* diagnosticTopic = "MUTHUR/DDATA/BASE/DIAG"; // Topic for diagnostic data

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

// BLE Nordic UART Service UUIDs for Nicla Voice
const char* niclaUartServiceUUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* niclaTxCharUUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // R4 writes to this (Nicla reads)
const char* niclaRxCharUUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // R4 reads from this (Nicla writes)

BLEDevice niclaDevice;
BLECharacteristic niclaTxChar;
BLECharacteristic niclaRxChar;

unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 1000; // Read sensor data every 1 second

// Diagnostics variables
unsigned long lastDiagnosticTime = 0;
const unsigned long diagnosticInterval = 5000; // Send diagnostic data every 5 seconds
unsigned long heartbeatCounter = 0;

// BLE status tracking
bool bleInitialized = false;
unsigned long lastBleCheckTime = 0;

// Manual switch on pin 5
const int switchPin = 5;
bool switchState = false;
bool lastSwitchState = false;
unsigned long lastSwitchDebounceTime = 0;
const unsigned long switchDebounceDelay = 50; // Debounce delay in milliseconds
const char* switchTopic = "MUTHUR/NDATA/BASE/SWITCH"; // Topic for switch state data

// Solid State Relay on pin 7
const int relayPin = 7;
bool relayState = false;
const char* relayControlTopic = "MUTHUR/DDATA/AIRPMP/WRITE"; // Topic for relay control commands
const char* relayStatusTopic = "MUTHUR/NDATA/BASE/RELAY"; // Topic for relay status data

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  
  // Check for WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true); // Don't continue
  }
  
  // Attempt to connect to WiFi network
  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, password);
    delay(10000); // Wait 10 seconds for connection
  }
  
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTT() {
  Serial.print("Connecting to MQTT broker at ");
  Serial.print(broker);
  Serial.print(":");
  Serial.print(port);
  Serial.println("...");
  
  while (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    delay(5000);
  }
  
  Serial.println("Connected to MQTT broker");
}

bool connectToNicla() {
  Serial.println("Scanning for Nicla Voice...");
  
  BLE.scanForUuid(niclaUartServiceUUID);
  
  BLEDevice peripheral = BLE.available();
  if (peripheral) {
    Serial.print("Found device: ");
    Serial.println(peripheral.localName());
    
    if (peripheral.localName().indexOf("Nicla") >= 0 || peripheral.hasService(niclaUartServiceUUID)) {
      BLE.stopScan();
      
      Serial.println("Connecting to Nicla Voice...");
      if (peripheral.connect()) {
        Serial.println("Connected to Nicla Voice");
        niclaDevice = peripheral;
        
        // Discover the UART service and characteristics
        if (peripheral.discoverService(niclaUartServiceUUID)) {
          BLEService uartService = peripheral.service(niclaUartServiceUUID);
          
          if (uartService) {
            niclaTxChar = uartService.characteristic(niclaTxCharUUID);
            niclaRxChar = uartService.characteristic(niclaRxCharUUID);
            
            if (niclaRxChar && niclaTxChar) {
              // Subscribe to notifications from Nicla's RX characteristic
              if (niclaRxChar.canSubscribe() && niclaRxChar.subscribe()) {
                Serial.println("Subscribed to Nicla notifications");
                
                // First check IMU status
                sendCommand("imu status");
                delay(200);
                
                // Now enable the sensors
                sendCommand("enable accel");
                delay(100);
                sendCommand("enable gyro");
                delay(100);
                
                // Request a calibration if needed
                sendCommand("calibrate imu");
                
                return true;
              } else {
                Serial.println("Failed to subscribe to Nicla notifications");
              }
            } else {
              Serial.println("Required characteristics not found");
            }
          } else {
            Serial.println("UART service not found");
          }
        } else {
          Serial.println("Failed to discover services");
        }
        
        // If we got here, something went wrong
        peripheral.disconnect();
      } else {
        Serial.println("Failed to connect to Nicla");
      }
    }
  }
  
  return false;
}

void sendCommand(String command) {
  if (niclaTxChar && niclaTxChar.canWrite()) {
    niclaTxChar.writeValue((const uint8_t*)command.c_str(), command.length());
    Serial.print("Sent command to Nicla: ");
    Serial.println(command);
  }
}

void requestSensorData() {
  if (niclaTxChar && niclaTxChar.canWrite()) {
    // Alternate between requesting accelerometer and gyroscope data
    static bool requestAccel = true;
    
    if (requestAccel) {
      sendCommand("get accel");
    } else {
      sendCommand("get gyro");
    }
    
    requestAccel = !requestAccel;
  }
}

void processSensorData() {
  if (niclaRxChar && niclaRxChar.valueUpdated()) {
    byte buffer[128];
    int bytesRead = niclaRxChar.readValue(buffer, sizeof(buffer));
    
    if (bytesRead > 0) {
      // Convert the received bytes to a string
      String data = "";
      for (int i = 0; i < bytesRead; i++) {
        data += (char)buffer[i];
      }
      
      Serial.print("Received data: ");
      Serial.println(data);
      
      // Process the data based on the prefix
      if (data.startsWith("accel:")) {
        // Extract acceleration values and publish to MQTT
        data = "Accelerometer: " + data.substring(6); // Remove "accel:" prefix
        
        mqttClient.beginMessage(topic);
        mqttClient.print(data);
        mqttClient.endMessage();
        
        Serial.println("Published to MQTT: " + data);
      }
      else if (data.startsWith("gyro:")) {
        // Extract gyroscope values and publish to MQTT
        data = "Gyroscope: " + data.substring(5); // Remove "gyro:" prefix
        
        mqttClient.beginMessage(topic);
        mqttClient.print(data);
        mqttClient.endMessage();
        
        Serial.println("Published to MQTT: " + data);
      }
      else if (data.startsWith("ERROR:")) {
        // Handle error messages from Nicla
        String errorMessage = "Nicla Error: " + data.substring(6);
        
        mqttClient.beginMessage(diagnosticTopic);
        mqttClient.print("{\"nicla_error\": \"" + data.substring(6) + "\"}");
        mqttClient.endMessage();
        
        Serial.println(errorMessage);
        
        // If there are repeated IMU errors, try to reset the Nicla IMU
        static int errorCount = 0;
        if (data.indexOf("IMU") >= 0 || data.indexOf("sensor") >= 0) {
          errorCount++;
          if (errorCount >= 3) {
            Serial.println("Multiple IMU errors detected, requesting IMU reset...");
            sendCommand("reset imu");
            errorCount = 0;
          }
        }
      }
      else if (data.startsWith("Nicla Voice Diagnostics:")) {
        // Store the Nicla diagnostic data for inclusion in our diagnostic messages
        static String niclaStatus = data;
        
        // Publish to MQTT as well
        mqttClient.beginMessage(diagnosticTopic);
        mqttClient.print("{\"nicla_diagnostics\": \"" + data.substring(23) + "\"}");
        mqttClient.endMessage();
      }
    }
  }
}

void sendDiagnosticData() {
  // Get WiFi signal strength (RSSI)
  long rssi = WiFi.RSSI();
  
  // Increment heartbeat counter
  heartbeatCounter++;
  
  // Request diagnostics from Nicla periodically to include in our diagnostic data
  static String niclaStatus = "unknown";
  static unsigned long lastNiclaDiagRequest = 0;
  
  if (millis() - lastNiclaDiagRequest > 15000 && niclaDevice && niclaDevice.connected()) {
    sendCommand("diagnostics");
    lastNiclaDiagRequest = millis();
  }
  
  // Create diagnostic message
  String diagnosticMessage = "{";
  diagnosticMessage += "\"device\": \"Arduino R4 WiFi\",";
  diagnosticMessage += "\"firmware\": \"1.1.0\",";
  diagnosticMessage += "\"heartbeat\": " + String(heartbeatCounter) + ",";
  diagnosticMessage += "\"rssi\": " + String(rssi) + ",";
  diagnosticMessage += "\"uptime\": " + String(millis() / 1000) + ",";
  diagnosticMessage += "\"nicla_connected\": " + String((niclaDevice && niclaDevice.connected()) ? "true" : "false") + ",";
  diagnosticMessage += "\"nicla_status\": \"" + niclaStatus + "\",";
  diagnosticMessage += "\"switch_state\": " + String(switchState ? "true" : "false") + ",";
  diagnosticMessage += "\"relay_state\": " + String(relayState ? "true" : "false");
  diagnosticMessage += "}";
  
  // Send diagnostic data to MQTT
  mqttClient.beginMessage(diagnosticTopic);
  mqttClient.print(diagnosticMessage);
  mqttClient.endMessage();
  
  Serial.print("Diagnostic data sent: ");
  Serial.println(diagnosticMessage);
}

void setup() {
  Serial.begin(115200);
  delay(3000); // Give time for Serial Monitor to open
  
  Serial.println("Arduino R4 WiFi with Nicla Voice - BLE Example");
  
  // Initialize switch pin
  pinMode(switchPin, INPUT_PULLUP);  // Using internal pull-up resistor
  switchState = !digitalRead(switchPin);  // Invert the reading
  lastSwitchState = !digitalRead(switchPin);
  Serial.print("Switch initialized on pin ");
  Serial.print(switchPin);
  Serial.print(", initial state: ");
  Serial.println(switchState ? "ON" : "OFF");
  
  // Initialize relay pin
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);  // Start with relay OFF
  relayState = false;
  Serial.print("Relay initialized on pin ");
  Serial.print(relayPin);
  Serial.println(", initial state: OFF");
  
  // Set initial relay state based on switch
  setRelayState(switchState, "initialization");
  
  // Initialize WiFi
  connectToWiFi();
  
  // Set MQTT client ID and connect to broker
  mqttClient.setId("ArduinoR4WiFi");  
  connectToMQTT();
  
  // Subscribe to relay control topic
  mqttClient.subscribe(relayControlTopic);
  Serial.print("Subscribed to MQTT topic: ");
  Serial.println(relayControlTopic);
  
  // Initialize BLE with retry mechanism
  int retryCount = 0;
  const int maxRetries = 5;
  
  while (!bleInitialized && retryCount < maxRetries) {
    Serial.print("Initializing BLE (attempt ");
    Serial.print(retryCount + 1);
    Serial.print("/");
    Serial.print(maxRetries);
    Serial.println(")...");
    
    if (BLE.begin()) {
      bleInitialized = true;
      Serial.println("BLE initialized successfully!");
    } else {
      Serial.println("Failed to initialize BLE, retrying...");
      retryCount++;
      
      // Power cycle the BLE module if possible
      BLE.end();
      delay(1000);
    }
  }
  
  if (!bleInitialized) {
    Serial.println("Failed to initialize BLE after multiple attempts!");
    Serial.println("The system will continue without BLE functionality.");
    Serial.println("Try the following:");
    Serial.println("1. Reset the board");
    Serial.println("2. Check hardware connections");
    Serial.println("3. Verify correct board selection in Arduino IDE");
  } else {
    Serial.println("BLE initialized, scanning for Nicla Voice...");
    BLE.setLocalName("ArduinoR4_BLE_Host");
  }
}

void loop() {
  // Keep MQTT connection alive
  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected, reconnecting...");
    connectToMQTT();
    // Re-subscribe to relay control topic after reconnection
    mqttClient.subscribe(relayControlTopic);
  }
  
  // Process incoming MQTT messages
  processIncomingMQTT();
    // Check if it's time to send diagnostic data
  unsigned long currentTime = millis();
  if (currentTime - lastDiagnosticTime >= diagnosticInterval) {
    sendDiagnosticData();
    lastDiagnosticTime = currentTime;
  }
  
  // Try to initialize BLE periodically if it failed before
  if (!bleInitialized && (currentTime - lastBleCheckTime > 30000)) { // Try every 30 seconds
    lastBleCheckTime = currentTime;
    Serial.println("Attempting to initialize BLE...");
    
    if (BLE.begin()) {
      bleInitialized = true;
      Serial.println("BLE initialized successfully!");
      BLE.setLocalName("ArduinoR4_BLE_Host");
    } else {
      Serial.println("BLE initialization still failing...");
    }
  }
  
  if (bleInitialized) {
    // Connect to Nicla if not connected
    if (!niclaDevice || !niclaDevice.connected()) {
      BLE.stopScan();
      if (connectToNicla()) {
        Serial.println("Successfully connected to Nicla Voice");
      } else {
        Serial.println("Failed to connect to Nicla Voice, will retry...");
        delay(2000);
      }
    } else {
      // We're connected to Nicla
      
      // Read sensor data at the specified interval
      if (currentTime - lastSensorReadTime >= sensorReadInterval) {
        requestSensorData();
        lastSensorReadTime = currentTime;
      }
      
      // Process any incoming sensor data
      processSensorData();
      
      // Periodic IMU health check and reset
      static unsigned long lastIMUCheckTime = 0;
      if (currentTime - lastIMUCheckTime >= 120000) { // Every 2 minutes
        Serial.println("Performing periodic IMU health check...");
        sendCommand("imu status");
        lastIMUCheckTime = currentTime;
      }
    }
    
    // Poll BLE events
    BLE.poll();
  } else {
    // BLE not available, continue with other functions
    // This allows the system to still send diagnostic data via WiFi/MQTT
    delay(100); // Small delay to prevent tight loop
  }
  
  // Monitor switch state (works regardless of BLE status)
  readSwitchState();
}

void readSwitchState() {
  // Read the current switch state (inverted)
  bool currentSwitchReading = !digitalRead(switchPin);
  
  // Check if the switch state has changed (debouncing)
  if (currentSwitchReading != lastSwitchState) {
    lastSwitchDebounceTime = millis();
  }
  
  // If enough time has passed since the last change, consider it a valid change
  if ((millis() - lastSwitchDebounceTime) > switchDebounceDelay) {
    // If the switch state has actually changed
    if (currentSwitchReading != switchState) {
      switchState = currentSwitchReading;
      
      // Control the relay based on switch state
      setRelayState(switchState, "switch");
      
      // Create switch state message
      String switchMessage = "{";
      switchMessage += "\"device\": \"Arduino R4 WiFi\",";
      switchMessage += "\"switch_pin\": " + String(switchPin) + ",";
      switchMessage += "\"switch_state\": " + String(switchState ? "true" : "false") + ",";
      switchMessage += "\"timestamp\": " + String(millis());
      switchMessage += "}";
      
      // Send switch state to MQTT
      mqttClient.beginMessage(switchTopic);
      mqttClient.print(switchMessage);
      mqttClient.endMessage();
      
      Serial.print("Switch state changed: ");
      Serial.print(switchState ? "ON" : "OFF");
      Serial.print(" - Published to MQTT: ");
      Serial.println(switchMessage);
    }
  }
  
  // Save the current reading for next time
  lastSwitchState = currentSwitchReading;
}

void setRelayState(bool newState, String source) {
  if (newState != relayState) {
    relayState = newState;
    digitalWrite(relayPin, relayState ? HIGH : LOW);
    
    // Create relay status message
    String relayMessage = "{";
    relayMessage += "\"device\": \"Arduino R4 WiFi\",";
    relayMessage += "\"relay_pin\": " + String(relayPin) + ",";
    relayMessage += "\"relay_state\": " + String(relayState ? "true" : "false") + ",";
    relayMessage += "\"control_source\": \"" + source + "\",";
    relayMessage += "\"timestamp\": " + String(millis());
    relayMessage += "}";
    
    // Send relay status to MQTT
    mqttClient.beginMessage(relayStatusTopic);
    mqttClient.print(relayMessage);
    mqttClient.endMessage();
    
    Serial.print("Relay state changed to ");
    Serial.print(relayState ? "ON" : "OFF");
    Serial.print(" by ");
    Serial.print(source);
    Serial.print(" - Published to MQTT: ");
    Serial.println(relayMessage);
  }
}

void processIncomingMQTT() {
  int messageSize = mqttClient.parseMessage();
  if (messageSize) {
    String topic = mqttClient.messageTopic();
    String message = "";
    
    while (mqttClient.available()) {
      message += (char)mqttClient.read();
    }
    
    Serial.print("Received MQTT message on topic: ");
    Serial.print(topic);
    Serial.print(" - Message: ");
    Serial.println(message);
    
    // Check if it's a relay control command
    if (topic == relayControlTopic) {
      if (message == "1") {
        setRelayState(true, "MQTT");
      } else if (message == "0") {
        setRelayState(false, "MQTT");
      } else {
        Serial.println("Invalid relay command. Use '1' for ON or '0' for OFF");
      }
    }
  }
}
