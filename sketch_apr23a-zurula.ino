// ESP32 + Waveshare 7.3" ACeP E-Ink Display

#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <GxEPD2_7C.h>
#include <Adafruit_GFX.h>

#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_ACeP_730

#define MAX_HEIGHT 96 

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT> display(
  GxEPD2_DRIVER_CLASS(/*CS=*/5, /*DC=*/27, /*RST=*/26, /*BUSY=*/25)
);

#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== ESP32 BOOTED ===");
  Serial.println("Free heap: " + String(ESP.getFreeHeap()));

  Serial.println("Montando SPIFFS...");
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ Error al montar SPIFFS");
    return;
  }
  Serial.println("SPIFFS montado OK");

  // ---- WIFI ----
  Serial.print("Conectando a WiFi ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    retries++;
    if (retries > 30) {
      Serial.println("\n❌ WiFi no conectó, durmiendo 1 min");
      esp_deep_sleep(60ULL * 1000000ULL);
    }
  }
  Serial.println("\nWiFi conectado!");
  Serial.println(WiFi.localIP());

  // ---- DISPLAY ----
  Serial.println("Inicializando display...");
  display.init(115200);
  display.setRotation(3);
  Serial.println("Display inicializado OK");
}

void loop() {
  Serial.println("\n==============================");
  Serial.println("==        LOOP START        ==");
  Serial.println("==============================");

  Serial.println("Consultando API... " + String(apiURL));

  HTTPClient http;
  http.begin(apiURL);
  http.addHeader("X-Device", "esp32");
  int httpCode = http.GET();

  Serial.println("HTTP code: " + String(httpCode));

  if (httpCode != 200) {
    Serial.println("❌ Error obteniendo URL imagen");
    http.end();
    Serial.println("Durmiendo 5 minutos");
    esp_deep_sleep(5ULL * 60ULL * 1000000ULL);
  }

  String payload = http.getString();
  http.end();

  Serial.println("Payload recibido:");
  Serial.println(payload);

  int start = payload.indexOf("http");
  int end = payload.indexOf("\"", start);
  if (start == -1 || end == -1) {
    Serial.println("❌ No se encontró URL de imagen en payload");
    esp_deep_sleep(5ULL * 60ULL * 1000000ULL);
  }

  String imageUrl = payload.substring(start, end);
  Serial.println("URL detectada:");
  Serial.println(imageUrl);

  // ---- DESCARGA BMP ----
  Serial.println("\nDescargando BMP...");
  HTTPClient imgHttp;
  imgHttp.begin(imageUrl);
  int imgCode = imgHttp.GET();

  Serial.println("Imagen HTTP code: " + String(imgCode));

  if (imgCode != 200) {
    Serial.println("❌ No se pudo descargar BMP");
    imgHttp.end();
    esp_deep_sleep(5ULL * 60ULL * 1000000ULL);
  }

  WiFiClient *stream = imgHttp.getStreamPtr();

  Serial.println("Abriendo archivo /latest.bmp en SPIFFS...");
  File f = SPIFFS.open("/latest.bmp", FILE_WRITE);
  if (!f) {
    Serial.println("❌ No se pudo crear archivo BMP en SPIFFS");
    imgHttp.end();
    esp_deep_sleep(5ULL * 60ULL * 1000000ULL);
  }

  uint8_t buff[128];
  int len = imgHttp.getSize();
  int total = 0;

  Serial.println("Iniciando escritura de BMP...");
  Serial.println("Tamaño reportado: " + String(len));

  while (imgHttp.connected() && (len > 0 || len == -1)) {
    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(buff, (size > sizeof(buff) ? sizeof(buff) : size));
      f.write(buff, c);
      total += c;
      if (len > 0) len -= c;
    }
    delay(1);
  }

  f.close();
  imgHttp.end();

  Serial.println("BMP descargado. Tamaño final: " + String(total));

  // ---- DIBUJAR ----
  Serial.println("Llamando a drawBMPFromSPIFFS...");
  drawBMPFromSPIFFS("/latest.bmp");

  // ---- APAGAR ----
  Serial.println("Desconectando WiFi...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("Hibernate display...");
  display.hibernate();

  Serial.println("🌙 Durmiendo 30 minutos...");


  esp_deep_sleep(30ULL * 60ULL * 1000000ULL);
}


uint16_t matchToACePColor(uint8_t r, uint8_t g, uint8_t b) {
  if (r < 64 && g < 64 && b < 64) return GxEPD_BLACK;
  if (r > 200 && g > 200 && b > 200) return GxEPD_WHITE;
  if (r > 200 && g < 80 && b < 80) return GxEPD_RED;
  if (r > 200 && g > 200 && b < 80) return GxEPD_YELLOW;
  if (r > 200 && g > 100 && b < 64) return GxEPD_ORANGE;
  if (b > 150 && r < 100 && g < 100) return GxEPD_BLUE;
  if (g > 100 && r < 100 && b < 100) return GxEPD_GREEN;
  return GxEPD_WHITE;
}


void drawBMPFromSPIFFS(const char* filename) {
  File bmpFile = SPIFFS.open(filename, "r");
  if (!bmpFile) {
    Serial.println("❌ No se pudo abrir BMP");
    return;
  }

  uint8_t header[54];
  if (bmpFile.read(header, 54) != 54) {
    Serial.println("❌ Error leyendo header BMP");
    bmpFile.close();
    return;
  }

  uint32_t dataOffset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
  int32_t bmpWidth  = header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24);
  int32_t bmpHeight = header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24);
  uint16_t depth = header[28] | (header[29] << 8);

  Serial.printf("BMP: %dx%d, %d bits\n", bmpWidth, bmpHeight, depth);

  if (depth != 24) {
    Serial.println("❌ Solo BMP 24 bits");
    bmpFile.close();
    return;
  }

  uint32_t rowSize = ((depth * bmpWidth + 31) / 32) * 4;

  display.setRotation(3);  // ✅ Portrait mode
  display.setFullWindow();
  display.firstPage();

  do {
    for (int y = 0; y < bmpHeight; y++) {
      int bmpRow = bmpHeight - 1 - y;
      bmpFile.seek(dataOffset + bmpRow * rowSize);
      
      uint8_t row[rowSize];
      bmpFile.read(row, rowSize);

      for (int x = 0; x < bmpWidth; x++) {
        uint8_t b = row[x * 3];
        uint8_t g = row[x * 3 + 1];
        uint8_t r = row[x * 3 + 2];

        uint16_t color = matchToACePColor(r, g, b);
        display.drawPixel(x, y, color);
      }
    }
  } while (display.nextPage());

  bmpFile.close();
  Serial.println("✅ Imagen dibujada correctamente");
}




