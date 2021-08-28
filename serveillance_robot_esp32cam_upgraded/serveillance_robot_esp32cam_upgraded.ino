#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"
#include "esp_camera.h"
#include "esp32-hal-ledc.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFi.h>

#define CAMERA_MODEL_AI_THINKER

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

/* Wifi Crdentials */ /* Replace your SSID and Password */
const char* ssid = "your ssid";
const char* password = "your password";

/* Defining DC motor, Servo and Flash LED pins */
const int RMotor1 = 14;
const int RMotor2 = 15;
const int LMotor1 = 13;
const int LMotor2 = 12;
const int panServo = 2;
const int tiltServo = 3;
const int FlashPin = 4;
/* Defining initial values */
int speed = 255;
int panVal = 4875;
int tiltVal = 4875;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

/* Stream handler */
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      {
        if (fb->format != PIXFORMAT_JPEG) {
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if (!jpeg_converted) {
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      break;
    }
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    /*Serial.printf("MJPG: %uB %ums (%.1ffps)\n",
                  (uint32_t)(_jpg_buf_len),
                  (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time
                 );*/
  }

  last_frame = 0;
  return res;
}

/* Command handler */
static esp_err_t cmd_handler(httpd_req_t *req)
{
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;
  /* Flash LED control */
  if (!strcmp(variable, "flash"))
  {
    ledcWrite(3, val);
  }
  else if (!strcmp(variable, "speed"))
  {
    /* Setting the motor speed */
    if      (val > 255) val = 255;
    else if (val <   0) val = 0;
    speed = val;
  }
  /* Robot direction control */
  else if (!strcmp(variable, "car")) {
    if (val == 1) {
      Serial.println("Forward");
      ledcWrite(4, speed);
      ledcWrite(5, 0);
      ledcWrite(6, 0);
      ledcWrite(7, speed);
    }
    else if (val == 2) {
      Serial.println("Turn Left");
      ledcWrite(4, speed);
      ledcWrite(5, 0);
      ledcWrite(6, speed);
      ledcWrite(7, 0);
    }
    else if (val == 3) {
      Serial.println("Stop");
      ledcWrite(4, 0);
      ledcWrite(5, 0);
      ledcWrite(6, 0);
      ledcWrite(7, 0);
    }
    else if (val == 4) {
      Serial.println("Turn Right");
      ledcWrite(4, 0);
      ledcWrite(5, speed);
      ledcWrite(6, 0);
      ledcWrite(7, speed);

    }
    else if (val == 5) {
      Serial.println("Backward");
      ledcWrite(4, 0);
      ledcWrite(5, speed);
      ledcWrite(6, speed);
      ledcWrite(7, 0);
    }
  }
  /* Pan and Tilt servo control */
  else if (!strcmp(variable, "pantilt")) {
    if (val == 1) {
      Serial.println("Tilt Up");
      if      (tiltVal > 6500) tiltVal = 6500;
      else if (tiltVal < 3250) tiltVal = 3250;
      tiltVal = tiltVal + 200;
      ledcWrite(9, tiltVal);
    }
    else if (val == 2) {
      Serial.println("Pan Left");
      if      (panVal > 6500) panVal = 6500;
      else if (panVal < 3250) panVal = 3250;
      panVal = panVal + 200;
      ledcWrite(8, panVal);
    }
    else if (val == 3) {
      Serial.println("Pan Right");
      if      (panVal > 6500) panVal = 6500;
      else if (panVal < 3250) panVal = 3250;
      panVal = panVal - 200;
      ledcWrite(8, panVal);
    }
    else if (val == 4) {
      Serial.println("Tilt Down");
      if      (tiltVal > 6500) tiltVal = 6500;
      else if (tiltVal < 3250) tiltVal = 3250;
      tiltVal = tiltVal - 200;
      ledcWrite(9, tiltVal);
    }
  }
  else
  {
    Serial.println("variable");
    res = -1;
  }

  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}


static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';

  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

/* Index HTML page design */
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!doctype html>
<html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width,initial-scale=1">
        <title>Serveillance Robot</title>
        <style>
            .button {background-color: #0097b5;border: none;border-radius: 4px;color: white;padding: 10px 25px;text-align: center;font-size: 16px;margin: 4px 2px;cursor: pointer;}
            .slider {appearance: none;width: 70%;height: 15px;border-radius: 10px;background: #d3d3d3;outline: none;}
            .slider::-webkit-slider-thumb {appearance: none;appearance: none;width: 30px;height: 30px;border-radius: 50%;background: #0097b5;}
            .label {color: #0097b5;font-size: 18px;}
        </style>
    </head>
    <body>
    <div align=center> <img id= "camstream" src="" style='width:300px;'></div>
    <br/>
    <br/>
    <div align=center> 
    <button class="button" id="forward" ontouchstart="fetch(document.location.origin+'/control?var=car&val=1');" ontouchend="fetch(document.location.origin+'/control?var=car&val=3');" >Forward</button>
    </div>
    <br/>
    <div align=center> 
    <button class="button" id="turnleft" ontouchstart="fetch(document.location.origin+'/control?var=car&val=2');" ontouchend="fetch(document.location.origin+'/control?var=car&val=3');" >Turn Left</button>
    <button class="button" id="stop" onclick="fetch(document.location.origin+'/control?var=car&val=3');">Stop</button>
    <button class="button" id="turnright" ontouchstart="fetch(document.location.origin+'/control?var=car&val=4');" ontouchend="fetch(document.location.origin+'/control?var=car&val=3');" >Turn Right</button>
    </div>
    <br/>
     <div align=center> 
    <button class="button" id="backward" ontouchstart="fetch(document.location.origin+'/control?var=car&val=5');" ontouchend="fetch(document.location.origin+'/control?var=car&val=3');">Backward</button>
    </div>
    <br/>
    <div align=center> 
    <label class="label">Flash</label>
    <input type="range" class="slider" id="flash" min="0" max="255" value="0" onchange="try{fetch(document.location.origin+'/control?var=flash&val='+this.value);}catch(e){}">
    </div>
    <br/>
    <div align=center> 
    <label class="label">Speed</label>
    <input type="range" class="slider" id="speed" min="0" max="255" value="255" onchange="try{fetch(document.location.origin+'/control?var=speed&val='+this.value);}catch(e){}">
    </div>
     <br/>
    <div align=center> 
    <button class="button" id="tiltup" onclick="fetch(document.location.origin+'/control?var=pantilt&val=1');" >Tilt Up</button>
    </div>
    <div align=center> 
    <button class="button" id="panleft" onclick="fetch(document.location.origin+'/control?var=pantilt&val=2');" >Pan Left</button>
    <button class="button" id="panright" onclick="fetch(document.location.origin+'/control?var=pantilt&val=3');" >Pan Right</button>
    </div>
     <div align=center> 
    <button class="button" id="tiltdown" onclick="fetch(document.location.origin+'/control?var=pantilt&val=4');" >Tilt Down</button>
    </div> 
    <script>
        window.onload = document.getElementById("camstream").src = window.location.href.slice(0, -1) + ":81/stream";
    </script>
    </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

   httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };
    
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}


void initMotors()
{
  /* Configuring PWM channels for DC motors */
  /* ledcSetup(Channel, Frequency, Resolution) */
  ledcSetup(4, 2000, 8); /* 2000 hz PWM, 8-bit resolution and range from 0 to 255 */
  ledcSetup(5, 2000, 8); 
  ledcSetup(6, 2000, 8); 
  ledcSetup(7, 2000, 8); 
  /* Attaching the channel to the GPIO to be controlled */
  /* ledcAttachPin(GPIO, Channel) */
  ledcAttachPin(RMotor1, 4);
  ledcAttachPin(RMotor2, 5);
  ledcAttachPin(LMotor1, 6);
  ledcAttachPin(LMotor2, 7);
}

void initServo() {
  /* Configuring PWM channels for servo motors */
  ledcSetup(8, 50, 16); /*50 hz PWM, 16-bit resolution and range from 3250 to 6500 */
  ledcSetup(9, 50, 16); 
  ledcAttachPin(panServo, 8);
  ledcAttachPin(tiltServo, 9);
  /* Initializing servo motors */
  ledcWrite(8, panVal);
  ledcWrite(9, tiltVal);
}

void initFlash() {
  /* Configuring PWM channels for Falsh LED */
  ledcSetup(3, 5000, 8); /* 5000 hz PWM, 8-bit resolution and range from 0 to 255 */
  ledcAttachPin(FlashPin, 3);
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  initMotors();
  initServo();
  initFlash();
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);

  /* Connecting to WiFi */
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  for (int i = 0; i < 5; i++) {
    ledcWrite(3, 10);
    delay(50);
    ledcWrite(3, 0);
    delay(50);
  }
}

void loop() {

}
