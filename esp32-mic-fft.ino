/*------------------------------------------------------------------------------
  Code by squix78.

  Do you like this sample? Support my work by teleporting a coffee to me:
  https://www.paypal.com/paypalme2/squix78/5

  In this blog post I described how the code works: 
  https://blog.squix.org/2019/08/esp32-esp-eye-browser-based-spectrum-analyzer.html

  
*/
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Ticker.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

const i2s_port_t I2S_PORT = I2S_NUM_0;
const int BLOCK_SIZE = 512;

const double signalFrequency = 1000;
const double samplingFrequency = 10000;
const uint8_t amplitude = 150;

double vReal[BLOCK_SIZE];
double vImag[BLOCK_SIZE];
int32_t samples[BLOCK_SIZE];

String labels[] = {"125", "250", "500", "1K", "2K", "4K", "8K", "16K"};

arduinoFFT FFT = arduinoFFT(); /* Create FFT object */

// Connecting to the Internet
const char * ssid = "yourssid";
const char * password = "yourpassw0rd";

int bands[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// Running a web server
WebServer server(80);

// Adding a websocket to the server
WebSocketsServer webSocket = WebSocketsServer(81);

// Serving a web page (from flash memory)
// formatted as a string literal!
char webpage[] PROGMEM = R"=====(
<html>
<!-- Adding a data chart using Chart.js -->
<head>
  <script src='https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.5.0/Chart.min.js'></script>
</head>
<body onload="javascript:init()">
<h2>Browser Based ESP32-EYE Spectrum Analyzer</h2>
<div>
  <canvas id="chart" width="600" height="400"></canvas>
</div>
<!-- Adding a websocket to the client (webpage) -->
<script>
  var webSocket, dataPlot;
  var maxDataPoints = 20;
  const maxValue = 200000000;
  const maxLow = maxValue * 0.5;
  const maxMedium = maxValue * 0.2;
  const maxHigh = maxValue * 0.3;

  function init() {
    webSocket = new WebSocket('ws://' + window.location.hostname + ':81/');
    dataPlot = new Chart(document.getElementById("chart"), {
      type: 'bar',
      data: {
        labels: [],
        datasets: [{
          data: [],
          label: "Low",
          backgroundColor: "#D6E9C6"
        },
        {
          data: [],
          label: "Moderate",
          backgroundColor: "#FAEBCC"
        },
        {
          data: [],
          label: "High",
          backgroundColor: "#EBCCD1"
        },
        ]
      }, 
      options: {
          responsive: false,
          animation: false,
          scales: {
              xAxes: [{ stacked: true }],
              yAxes: [{
                  display: true,
                  stacked: true,
                  ticks: {
                    beginAtZero: true,
                    steps: 1000,
                    stepValue: 500,
                    max: maxValue
                  }
              }]
           }
       }
    });
    webSocket.onmessage = function(event) {
      var data = JSON.parse(event.data);
      dataPlot.data.labels = [];
      dataPlot.data.datasets[0].data = [];
      dataPlot.data.datasets[1].data = [];
      dataPlot.data.datasets[2].data = [];
      
      data.forEach(function(element) {
        dataPlot.data.labels.push(element.bin);
        var lowValue = Math.min(maxLow, element.value);
        dataPlot.data.datasets[0].data.push(lowValue);
        
        var mediumValue = Math.min(Math.max(0, element.value - lowValue), maxMedium);
        dataPlot.data.datasets[1].data.push(mediumValue);
        
        var highValue = Math.max(0, element.value - lowValue - mediumValue);
        dataPlot.data.datasets[2].data.push(highValue);

      });
      dataPlot.update();
    }
  }

</script>
</body>
</html>
)=====";


void setupMic() {
  Serial.println("Configuring I2S...");
  esp_err_t err;

  // The I2S config as per the example
  const i2s_config_t i2s_config = {
      .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX), // Receive, not transfer
      .sample_rate = samplingFrequency,                        
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // could only get it to work with 32bits
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // although the SEL config should be left, it seems to transmit on right
      .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,     // Interrupt level 1
      .dma_buf_count = 4,                           // number of buffers
      .dma_buf_len = BLOCK_SIZE                     // samples per buffer
  };

  // The pin config as per the setup
    i2s_pin_config_t pin_config = {
        .bck_io_num = 26,  // IIS_SCLK
        .ws_io_num = 32,   // IIS_LCLK
        .data_out_num = -1,// IIS_DSIN
        .data_in_num = 33  // IIS_DOUT
    };

  // Configuring the I2S driver and pins.
  // This function must be called before any I2S driver read/write operations.
  err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed installing driver: %d\n", err);
    while (true);
  }
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed setting pin: %d\n", err);
    while (true);
  }
  Serial.println("I2S driver installed.");
}



void setup() {
  // put your setup code here, to run once:
  WiFi.begin(ssid, password);
  Serial.begin(115200);
  while(WiFi.status()!=WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  
  delay(1000);
  Serial.println("Setting up mic");
  setupMic();
  Serial.println("Mic setup completed");

  delay(1000);

  server.on("/",[](){
    server.send_P(200, "text/html", webpage);
  });
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  // put your main code here, to run repeatedly:
  webSocket.loop();
  server.handleClient();

  // Read multiple samples at once and calculate the sound pressure

  size_t num_bytes_read;  
  esp_err_t read_success = i2s_read(I2S_PORT, 
                                    (char *)samples, 
                                    BLOCK_SIZE, 
                                    &num_bytes_read, 
                                    portMAX_DELAY);

  if(read_success != ESP_OK){
    Serial.println("Error reading i2s");
    return;  //kick out of this loop iteration
  }

  for (uint16_t i = 0; i < BLOCK_SIZE; i++) {
    vReal[i] = samples[i] << 8;
    vImag[i] = 0.0; //Imaginary part must be zeroed in case of looping to avoid wrong calculations and overflows
  }

  FFT.Windowing(vReal, BLOCK_SIZE, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.Compute(vReal, vImag, BLOCK_SIZE, FFT_FORWARD);
  FFT.ComplexToMagnitude(vReal, vImag, BLOCK_SIZE);
  for (int i = 0; i < 8; i++) {
    bands[i] = 0;
  }
  
  for (int i = 2; i < (BLOCK_SIZE/2); i++){ // Don't use sample 0 and only first SAMPLES/2 are usable. Each array eleement represents a frequency and its value the amplitude.
    if (vReal[i] > 2000) { // Add a crude noise filter, 10 x amplitude or more
      if (i<=2 )             bands[0] = max(bands[0], (int)(vReal[i]/amplitude)); // 125Hz
      if (i >3   && i<=5 )   bands[1] = max(bands[1], (int)(vReal[i]/amplitude)); // 250Hz
      if (i >5   && i<=7 )   bands[2] = max(bands[2], (int)(vReal[i]/amplitude)); // 500Hz
      if (i >7   && i<=15 )  bands[3] = max(bands[3], (int)(vReal[i]/amplitude)); // 1000Hz
      if (i >15  && i<=30 )  bands[4] = max(bands[4], (int)(vReal[i]/amplitude)); // 2000Hz
      if (i >30  && i<=53 )  bands[5] = max(bands[5], (int)(vReal[i]/amplitude)); // 4000Hz
      if (i >53  && i<=200 ) bands[6] = max(bands[6], (int)(vReal[i]/amplitude)); // 8000Hz
      if (i >200           ) bands[7] = max(bands[7], (int)(vReal[i]/amplitude)); // 16000Hz
    }

    //for (byte band = 0; band <= 6; band++) display.drawHorizontalLine(18*band,64-peak[band],14);
  }
  getData();
  /*for (int i = 0; i < 8; i++) {
    Serial.print(String(bands[i]));
    Serial.print(",");
  }
  Serial.println();*/
}

void getData() {

  String json = "[";
  for (int i = 0; i < 8; i++) {
    if (i > 0) {
      json +=", ";
    }
    json += "{\"bin\":";
    json += "\"" + labels[i] + "\"";
    json += ", \"value\":";
    json += String(bands[i]);
    json += "}"; 
  }
  json += "]";
  webSocket.broadcastTXT(json.c_str(), json.length());
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  // Do something with the data from the client
  if(type == WStype_TEXT){

  }
}
