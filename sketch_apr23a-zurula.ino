// ESP32 + Waveshare 7.3" ACeP E-Ink Display
// Muestra una imagen a color (7 colores) desde SPIFFS cada 3 minutos

#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <GxEPD2_7C.h>
#include <Adafruit_GFX.h>

#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_ACeP_730
#define MAX_HEIGHT 64

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT> display(
  GxEPD2_DRIVER_CLASS(/*CS=*/ 5, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)
);

#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
    return;
  }

  // Setup wifi 
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");

  display.init(115200);
  display.setRotation(0);
}

void loop() {
  Serial.println("\n==== LOOP ====");
  Serial.println("Consultando API para obtener la última imagen...");

  HTTPClient http;
  http.begin(apiURL);
  int httpCode = http.GET();
  if (httpCode != 200) {
    Serial.println("Error al obtener la URL de la imagen");
    http.end();
    delay(60000);
    return;
  }

  String payload = http.getString();
  http.end();
  Serial.println(payload);

  int start = payload.indexOf("http");
  int end = payload.indexOf("\"", start);
  String imageUrl = payload.substring(start, end);

  Serial.println("\n\nDescargando: " + imageUrl);

  HTTPClient imgHttp;
  imgHttp.begin(imageUrl);
  int imgCode = imgHttp.GET();
  if (imgCode != 200) {
    Serial.println("No se pudo descargar la imagen BMP.");
    imgHttp.end();
    delay(60000);
    return;
  }

  WiFiClient* stream = imgHttp.getStreamPtr();
  File f = SPIFFS.open("/latest.bmp", FILE_WRITE);
  if (!f) {
    Serial.println("No se pudo crear el archivo SPIFFS");
    imgHttp.end();
    delay(60000);
    return;
  }

  uint8_t buff[128];
  int len = imgHttp.getSize();
  int total = 0;
  while (imgHttp.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
      f.write(buff, c);
      total += c;
      if (len > 0) len -= c;
    }
    delay(1);
  }
  f.close();
  imgHttp.end();

  Serial.printf("Imagen guardada con tamaño: %d bytes\n", total);
  drawBMPFromSPIFFS("/latest.bmp");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  display.hibernate();

  Serial.println("Entrando en deep sleep por 30 minutos...");
  esp_deep_sleep(30 * 60 * 1000000ULL);

}

uint16_t matchToACePColor(uint8_t r, uint8_t g, uint8_t b) {
  if (r < 64 && g < 64 && b < 64) return GxEPD_BLACK;
  if (r > 200 && g > 200 && b > 200) return GxEPD_WHITE;
  if (r > 200 && g < 80 && b < 80) return GxEPD_RED;
  if (r > 200 && g > 200 && b < 80) return GxEPD_YELLOW;
  if (r > 200 && g > 100 && b < 64) return GxEPD_ORANGE;
  if (b > 150 && r < 100 && g < 100) return GxEPD_BLUE;
  if (g > 100 && r < 100 && b < 100) return GxEPD_GREEN;
  return GxEPD_WHITE; // fallback
}

void drawBMPFromSPIFFS(const char* filename) {
  File bmpFile = SPIFFS.open(filename, "r");
  if (!bmpFile) {
    Serial.println("No se pudo abrir la imagen BMP desde SPIFFS");
    return;
  }

  uint8_t header[54];
  if (bmpFile.read(header, 54) != 54) {
    Serial.println("Error leyendo el header BMP");
    return;
  }

  uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
  int32_t width  = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  int32_t height = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);

  uint16_t depth = header[28] | (header[29] << 8);
  if (depth != 24) {
    Serial.println("Solo se admiten BMP de 24 bits");
    bmpFile.close();
    return;
  }

  uint32_t rowSize = ((depth * width + 31) / 32) * 4;
  uint8_t row[rowSize];

  display.setFullWindow();
  display.firstPage();
  do {
    for (int16_t y = 0; y < height; y++) {
      uint32_t pos = dataOffset + (height - 1 - y) * rowSize;
      bmpFile.seek(pos);
      bmpFile.read(row, rowSize);
      for (int16_t x = 0; x < width; x++) {
        uint8_t b = row[x * 3];
        uint8_t g = row[x * 3 + 1];
        uint8_t r = row[x * 3 + 2];
        uint16_t color = matchToACePColor(r, g, b);
        display.drawPixel(x, y, color);

      }
    }
  } while (display.nextPage());

  bmpFile.close();
}



