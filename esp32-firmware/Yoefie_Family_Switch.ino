#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoOTA.h>
#include <cctype>
#include <time.h>

// ================ KONFIGURASI UTAMA ================
#define ID_ZONA "rumahku"
const char* ssid     = "YOEFIE FAMILY";
const char* password = "Yoefie1234";

#define API_URL         "https://smart.yoefiefamily.biz.id/api2.php"
#define API_KEY         "YoefieBiz_2026_Taman"

#define BOT_TOKEN       "8904033154:AAEAWLG1EWg4VzXTX2ofL2T5Eo3YrPXq8nk"
#define CHAT_ID_OWNER   "6259876551"

#define PIN_LED_PUSAT   2

// === KONFIGURASI LCD I2C ===
LiquidCrystal_I2C lcd(0x27, 16, 2);
#define WAKTU_TAMPIL 1500

// === KONFIGURASI WAKTU (NTP) ===
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// === KONFIGURASI WIFI AUTO-RECONNECT ===
#define WIFI_RECONNECT_INTERVAL 30000  // Cek WiFi tiap 30 detik
unsigned long lastWiFiCheck = 0;

// === FUNGSI UBAH TEKS AMAN UNTUK URL ===
String urlEncode(String teks) {
  String hasil = "";
  for (int i = 0; i < teks.length(); i++) {
    char c = teks.charAt(i);
    if (isalnum(c)) hasil += c;
    else if (c == ' ') hasil += "%20";
    else if (c == '-' || c == '_' || c == '.' || c == '~') hasil += c;
    else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      hasil += buf;
    }
  }
  return hasil;
}

// === DAFTAR ALAT ===
#define JUMLAH_ALAT 6
int pinAlat[JUMLAH_ALAT]      = {27,26,19,18,17,16};
String namaAlat[JUMLAH_ALAT]  = {
  "lampu kamar mandi",
  "lampu kamar ibu",
  "lampu kamar kk",
  "lampu ruang tamu",
  "lampu teras",
  "lampu dapur"
};
String statusAlat[JUMLAH_ALAT];

unsigned long lastSinkron = 0;
unsigned long lastCekBot  = 0;
unsigned long lastCekAntrian = 0;
unsigned long ledKedip    = 0;
unsigned long lcdGanti    = 0;
int indeksTampil = 0;
bool koneksiOK = false;

WebServer server(80);
WiFiClientSecure client;
UniversalTelegramBot bot(String(BOT_TOKEN), client);

// ====================================================
// 📶 FUNGSI: CEK & AUTO-RECONNECT WIFI
// ====================================================
void cekWiFiKoneksi() {
  if (millis() - lastWiFiCheck < WIFI_RECONNECT_INTERVAL) return;
  lastWiFiCheck = millis();

  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("📶 WiFi putus! Menyambung ulang...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("WIFI PUTUS!");
  lcd.setCursor(0, 1); lcd.print("Menyambung...");

  int percobaan = 0;
  while (WiFi.status() != WL_CONNECTED && percobaan < 20) {
    delay(500);
    percobaan++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi tersambung! IP: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WIFI TERHUBUNG");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP().toString());
    delay(2000);
  }
}

// ====================================================
// 🕒 FUNGSI CEK TIMER OTOMATIS BERDASARKAN JAM
// ====================================================
void cekTimerOtomatis() {
  if(WiFi.status() != WL_CONNECTED) return;
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }

  int jam = timeinfo.tm_hour;
  int menit = timeinfo.tm_min;

  // Lampu Teras (Indeks 4) — Nyala 18:00, Mati 06:00
  if (jam == 18 && menit == 0) {
    if (statusAlat[4] != "ON") {
      aturAlat(4, "ON");
      kirimPerintah(namaAlat[4], "ON");
    }
  } else if (jam == 6 && menit == 0) {
    if (statusAlat[4] != "OFF") {
      aturAlat(4, "OFF");
      kirimPerintah(namaAlat[4], "OFF");
    }
  }
}

// ====================================================
// ✅ FUNGSI CEK PERINTAH DARI ANTRIAN WEB
// ====================================================
void cekPerintahAntrian() {
  if(WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String alamat = String(API_URL) + "?api_key=" + API_KEY + "&zona=" + ID_ZONA + "&cek_perintah=1";
  
  http.begin(alamat.c_str());
  int kode = http.GET();

  if(kode == 200) {
    String jawaban = http.getString();
    DynamicJsonDocument doc = DynamicJsonDocument(512);
    DeserializationError err = deserializeJson(doc, jawaban);

    if(!err && doc["status"] == "ada_perintah") {
      const char* namaDariWeb = doc["alat"];
      const char* aksiDariWeb = doc["aksi"];
      int idPerintah = doc["id"];

      if(String(namaDariWeb) == "SEMUA_ALAT"){
        for(int i=0; i<JUMLAH_ALAT; i++){
          aturAlat(i, String(aksiDariWeb));
        }
      } else {
        for(int i=0; i<JUMLAH_ALAT; i++){
          if(namaAlat[i] == String(namaDariWeb)){
            aturAlat(i, String(aksiDariWeb));
            break;
          }
        }
      }

      String tandaSelesai = String(API_URL) + "?api_key=" + API_KEY + "&tandai_selesai=1&id=" + idPerintah;
      HTTPClient http2;
      http2.begin(tandaSelesai.c_str());
      http2.GET();
      http2.end();
    }
  }
  http.end();
}
        
// === PERBARUI TAMPILAN LCD ===
void perbaruiLCD(){
  if(millis() - lcdGanti < WAKTU_TAMPIL) return;
  lcdGanti = millis();
  lcd.clear();

  if(indeksTampil == 0){
    lcd.setCursor(0,0);
    lcd.print("ZONA: " + String(ID_ZONA));
    lcd.setCursor(0,1);
    lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi: TERHUBUNG" : "WiFi: PUTUS");
  }
  else if(indeksTampil == 1){
    lcd.setCursor(0,0);
    lcd.print("SERVER API");
    lcd.setCursor(0,1);
    lcd.print(koneksiOK ? "TERHUBUNG OK" : "MENUNGGU...");
  }
  else {
    int noAlat = indeksTampil - 2;
    if(noAlat < JUMLAH_ALAT){
      lcd.setCursor(0,0);
      String tampilNama = namaAlat[noAlat];
      if(tampilNama.length()>15) tampilNama = tampilNama.substring(0,14)+".";
      lcd.print(tampilNama);
      lcd.setCursor(0,1);
      lcd.print("STATUS: ");
      lcd.print(statusAlat[noAlat] == "ON" ? "NYALA" : "MATI");
    }
  }

  int totalLayar = 2 + JUMLAH_ALAT;
  indeksTampil++;
  if(indeksTampil >= totalLayar) indeksTampil = 0;
}

// === ATUR STATUS ALAT ===
void aturAlat(int indeks, String status){
  if(status == statusAlat[indeks]) return;
  statusAlat[indeks] = status;
  digitalWrite(pinAlat[indeks], (status=="ON") ? HIGH : LOW);
}

// === INDIKATOR LED ===
void perbaruiLED(){
  if(!koneksiOK){
    if(millis()-ledKedip>500){
      ledKedip=millis();
      digitalWrite(PIN_LED_PUSAT, !digitalRead(PIN_LED_PUSAT));
    }
  } else {
    digitalWrite(PIN_LED_PUSAT, HIGH);
  }
}

// === SINKRONISASI SERVER ===
void sinkronServer(){
  koneksiOK = false;
  if(WiFi.status()!=WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(API_URL) + "?api_key=" + String(API_KEY) + "&zona=" + String(ID_ZONA));
  int res = http.GET();
  if(res==200){
    koneksiOK = true;
    DynamicJsonDocument doc = DynamicJsonDocument(512);
    if(deserializeJson(doc,http.getString())==DeserializationError::Ok){
      for(int i=0;i<JUMLAH_ALAT;i++){
        if(doc.containsKey(namaAlat[i])){
          aturAlat(i, doc[namaAlat[i]].as<String>());
        }
      }
    }
  }
  http.end();
}

// === KIRIM PERINTAH KE SERVER ===
void kirimPerintah(String alat, String aksi){
  if(WiFi.status()!=WL_CONNECTED) return;
  HTTPClient http; 
  http.begin(API_URL);
  http.addHeader("Content-Type","application/x-www-form-urlencoded");
  String data = "api_key="+String(API_KEY)+
                "&zona="+String(ID_ZONA)+
                "&nama="+urlEncode(alat)+
                "&aksi="+urlEncode(aksi);
  http.POST(data); 
  http.end();
}

// === HALAMAN WEB LOKAL ===
String halamanWebLokal(){
  String isi = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  isi += "<title>Zona: " + String(ID_ZONA) + "</title>";
  isi += "<style>body{font-family:Arial;max-width:600px;margin:auto;padding:15px;} .tombol{padding:8px 14px;margin:4px;border:none;border-radius:5px;color:white;text-decoration:none;display:inline-block;} .h{background:#28a745;} .m{background:#dc3545;}</style></head><body>";
  isi += "<h1> Zona: " + String(ID_ZONA) + "</h1><hr>";
  for(int i=0;i<JUMLAH_ALAT;i++){
    isi += "<p><b>"+namaAlat[i]+"</b> : "+statusAlat[i];
    isi += " <a href='/on/"+String(i)+"' class='tombol h'>NYALA</a>";
    isi += " <a href='/off/"+String(i)+"' class='tombol m'>MATI</a></p>";
  }
  isi += "</body></html>";
  return isi;
}

// === PERINTAH TELEGRAM ===
void cekTelegram(){
  if(millis()-lastCekBot<2000) return; 
  lastCekBot=millis();
  int jml = bot.getUpdates(bot.last_message_received + 1);
  if(jml > 0){
    for(int i=0; i<jml; i++){
      String id = String(bot.messages[i].chat_id);
      String teks = bot.messages[i].text;
      teks.trim();
      teks.toLowerCase();

      if(teks == "/start" || teks == "/status"){
        String balasan = " LAPORAN ZONA: " + String(ID_ZONA) + "\n";
        balasan += " WiFi: " + String(WiFi.status()==WL_CONNECTED ? "TERHUBUNG " : "PUTUS ") + "\n\n";
        balasan += " DAFTAR PERANGKAT:\n";
        for(int i=0;i<JUMLAH_ALAT;i++){
          balasan += "- " + namaAlat[i] + " : " + statusAlat[i] + "\n";
        }
        balasan += "\n PERINTAH CEPAT:\n";
        balasan += "• /semuanyahidup - Nyalakan semua\n";
        balasan += "• /semuanyamat - Matikan semua\n";
        balasan += "• /bantuan - Panduan lengkap\n";
        bot.sendMessage(id,balasan,"");
      }
      else if(teks == "/semuanyahidup"){
        for(int i=0;i<JUMLAH_ALAT;i++){
          kirimPerintah(namaAlat[i],"ON");
          aturAlat(i,"ON");
        }
        bot.sendMessage(id," SEMUA PERANGKAT SUDAH DINYALAKAN","");
      }
      else if(teks == "/semuanyamati"){
        for(int i=0;i<JUMLAH_ALAT;i++){
          kirimPerintah(namaAlat[i],"OFF");
          aturAlat(i,"OFF");
        }
        bot.sendMessage(id," SEMUA PERANGKAT SUDAH DIMATIKAN","");
      }
      else if(teks == "/kamar_mandi"){
        kirimPerintah(namaAlat[0],"ON"); aturAlat(0,"ON");
        bot.sendMessage(id," Lampu kamar mandi nyala","");
      }
      else if(teks == "/kamar_ibu"){
        kirimPerintah(namaAlat[1],"ON"); aturAlat(1,"ON");
        bot.sendMessage(id," Lampu kamar ibu nyala","");
      }
      else if(teks == "/kamar_kk"){
        kirimPerintah(namaAlat[2],"ON"); aturAlat(2,"ON");
        bot.sendMessage(id," Lampu kamar kakak nyala","");
      }
      else if(teks == "/ruang_tamu"){
        kirimPerintah(namaAlat[3],"ON"); aturAlat(3,"ON");
        bot.sendMessage(id," Lampu ruang tamu nyala","");
      }
      else if(teks == "/teras"){
        kirimPerintah(namaAlat[4],"ON"); aturAlat(4,"ON");
        bot.sendMessage(id," Lampu teras nyala","");
      }
      else if(teks == "/dapur"){
        kirimPerintah(namaAlat[5],"ON"); aturAlat(5,"ON");
        bot.sendMessage(id," Lampu dapur nyala","");
      }
      else if(teks == "/kamar_mandi_nyala"){
        kirimPerintah(namaAlat[0],"ON"); aturAlat(0,"ON");
        bot.sendMessage(id," Lampu kamar mandi nyala","");
      }
      else if(teks == "/kamar_mandi_mati"){
        kirimPerintah(namaAlat[0],"OFF"); aturAlat(0,"OFF");
        bot.sendMessage(id," Lampu kamar mandi mati","");
      }
      else if(teks == "/kamar_ibu_nyala"){
        kirimPerintah(namaAlat[1],"ON"); aturAlat(1,"ON");
        bot.sendMessage(id," Lampu kamar ibu nyala","");
      }
      else if(teks == "/kamar_ibu_mati"){
        kirimPerintah(namaAlat[1],"OFF"); aturAlat(1,"OFF");
        bot.sendMessage(id," Lampu kamar ibu mati","");
      }
      else if(teks == "/kamar_kk_nyala"){
        kirimPerintah(namaAlat[2],"ON"); aturAlat(2,"ON");
        bot.sendMessage(id," Lampu kamar kakak nyala","");
      }
      else if(teks == "/kamar_kk_mati"){
        kirimPerintah(namaAlat[2],"OFF"); aturAlat(2,"OFF");
        bot.sendMessage(id," Lampu kamar kakak mati","");
      }
      else if(teks == "/ruang_tamu_nyala"){
        kirimPerintah(namaAlat[3],"ON"); aturAlat(3,"ON");
        bot.sendMessage(id," Lampu ruang tamu nyala","");
      }
      else if(teks == "/ruang_tamu_mati"){
        kirimPerintah(namaAlat[3],"OFF"); aturAlat(3,"OFF");
        bot.sendMessage(id," Lampu ruang tamu mati","");
      }
      else if(teks == "/teras_nyala"){
        kirimPerintah(namaAlat[4],"ON"); aturAlat(4,"ON");
        bot.sendMessage(id," Lampu teras nyala","");
      }
      else if(teks == "/teras_mati"){
        kirimPerintah(namaAlat[4],"OFF"); aturAlat(4,"OFF");
        bot.sendMessage(id," Lampu teras mati","");
      }
      else if(teks == "/dapur_nyala"){
        kirimPerintah(namaAlat[5],"ON"); aturAlat(5,"ON");
        bot.sendMessage(id," Lampu dapur nyala","");
      }
      else if(teks == "/dapur_mati"){
        kirimPerintah(namaAlat[5],"OFF"); aturAlat(5,"OFF");
        bot.sendMessage(id," Lampu dapur mati","");
      }
      else if(teks.indexOf("nyala ") == 0 || teks.indexOf("hidup ") == 0 || teks.indexOf("/nyala ") == 0){
        String nama = teks.substring(teks.indexOf(" ")+1);
        bool ditemukan = false;
        for(int i=0;i<JUMLAH_ALAT;i++){
          if(nama.equalsIgnoreCase(namaAlat[i])){ 
            kirimPerintah(nama,"ON"); 
            aturAlat(i,"ON");
            bot.sendMessage(id," " + namaAlat[i] + " Dinyalakan","");
            ditemukan = true;
            break;
          }
        }
        if(!ditemukan) bot.sendMessage(id," Nama alat tidak ditemukan! Cek ketikan Anda.","");
      }
      else if(teks.indexOf("mati ") == 0 || teks.indexOf("matikan ") == 0 || teks.indexOf("/mati ") == 0){
        String nama = teks.substring(teks.indexOf(" ")+1);
        bool ditemukan = false;
        for(int i=0;i<JUMLAH_ALAT;i++){
          if(nama.equalsIgnoreCase(namaAlat[i])){ 
            kirimPerintah(nama,"OFF"); 
            aturAlat(i,"OFF");
            bot.sendMessage(id," " + namaAlat[i] + " Dimatikan","");
            ditemukan = true;
            break;
          }
        }
        if(!ditemukan) bot.sendMessage(id," Nama alat tidak ditemukan! Cek ketikan Anda.","");
      }
      else if(teks == "/bantuan"){
        String bantu = " PANDUAN PENGGUNAAN:\n";
        bantu += "• /status - Lihat kondisi semua\n";
        bantu += "• /semuanyahidup - Nyalakan semua\n";
        bantu += "• /semuanyamat - Matikan semua\n";
        bantu += "• /ruang_tamu - Nyalakan ruang tamu\n";
        bantu += "• Ketik: nyala [nama alat]\n";
        bantu += "• Ketik: mati [nama alat]\n";
        bot.sendMessage(id,bantu,"");
      }
      else {
        bot.sendMessage(id," Perintah tidak dikenal.\nKetik /start atau /bantuan untuk panduan.","");
      }
    }
  }
}

// === SETUP AWAL ===
void setup(){
  Serial.begin(115200);
  pinMode(PIN_LED_PUSAT, OUTPUT);
  
  lcd.init();
  lcd.backlight();
  lcd.print("MEMULAI SISTEM...");

  for(int i=0;i<JUMLAH_ALAT;i++){
    pinMode(pinAlat[i],OUTPUT);
    digitalWrite(pinAlat[i],LOW);
    statusAlat[i]="OFF";
  }

  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){delay(500);Serial.print(".");}
  Serial.println("\n IP Lokal: " + WiFi.localIP().toString() + " | ZONA: " + String(ID_ZONA));
  
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nist.gov");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("ZONA: " + String(ID_ZONA));
  lcd.setCursor(0,1);
  lcd.print("WiFi: TERHUBUNG");

  String namaPerangkat = "esp32-" + String(ID_ZONA);
  ArduinoOTA.setHostname(namaPerangkat.c_str());
  ArduinoOTA.begin();

  client.setInsecure();
  server.on("/", [](){server.send(200,"text/html",halamanWebLokal());});
  
  server.onNotFound([](){
    String path = server.uri();
    if(path.startsWith("/on/")){
      int idx = path.substring(4).toInt();
      if(idx >=0 && idx < JUMLAH_ALAT){
        aturAlat(idx,"ON");
        kirimPerintah(namaAlat[idx],"ON");
        server.send(200,"text/html","<html><body style='text-align:center;padding:20px;'><h2> "+namaAlat[idx]+" DINYALAKAN</h2><br><a href='/'>Kembali ke Halaman Utama</a></body></html>");
      } else server.send(400,"text/plain","Alat tidak ditemukan");
    }
    else if(path.startsWith("/off/")){
      int idx = path.substring(5).toInt();
      if(idx >=0 && idx < JUMLAH_ALAT){
        aturAlat(idx,"OFF");
        kirimPerintah(namaAlat[idx],"OFF");
        server.send(200,"text/html","<html><body style='text-align:center;padding:20px;'><h2> "+namaAlat[idx]+" DIMATIKAN</h2><br><a href='/'>Kembali ke Halaman Utama</a></body></html>");
      } else server.send(404,"text/plain","Halaman tidak ada");
    }
  });

  server.begin();
  bot.sendMessage(CHAT_ID_OWNER," ZONA " + String(ID_ZONA) + " SIAP BEROPERASI\n OTA TERSEDIA","");
}

// === LOOP UTAMA ===
void loop(){
  ArduinoOTA.handle();
  server.handleClient();
  cekWiFiKoneksi();          // ✅ Auto-Reconnect WiFi
  cekTelegram();
  perbaruiLED();
  perbaruiLCD();

  static unsigned long lastCekTimer = 0;
  if(millis() - lastCekTimer > 30000) {
    lastCekTimer = millis();
    cekTimerOtomatis();
  }

  if(millis()-lastSinkron>5000){
    lastSinkron=millis();
    sinkronServer();
  }

  if(millis()-lastCekAntrian>2000){
    lastCekAntrian=millis();
    cekPerintahAntrian();
  }
}
