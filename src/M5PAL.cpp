#include <mwm5.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Ambient.h>

#include "config.h"

#define NTPHOST1 "ntp.nict.jp"
#define NTPHOST2 "ntp.jst.mfeed.ad.jp"
#define LINEHOST "notify-api.line.me"

AsciiParser parse_ascii(512);
Ambient ambient;
WiFiClientSecure client;
WiFiClient aclient;
struct tm timeInfo;

void wifiConnect() {
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);  // Wi-Fi接続
  int cnt=0;
  while (WiFi.status() != WL_CONNECTED) {  // Wi-Fi 接続待ち
    delay(100);
    Serial.printf("."); 
    if(++cnt == 30) M5.Power.reset();
  }
  Serial.println("\nwifi connect ok");
  configTime( 9*3600, 0, NTPHOST1, NTPHOST2);
}

void send(String message) {
  if(WiFi.status() != WL_CONNECTED) wifiConnect();
  const char* host = LINEHOST;
  client.setInsecure();
  if (!client.connect(host, 443)) {
    Serial.println("Connection failed");
    return;
  }
  Serial.println("Connected");
  String query = String("message=") + message;
  String request = String("") +
               "POST /api/notify HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Authorization: Bearer " + token + "\r\n" +
               "Content-Length: " + String(query.length()) +  "\r\n" + 
               "Content-Type: application/x-www-form-urlencoded\r\n\r\n" +
                query + "\r\n";
  client.print(request);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
//    Serial.println(line);
    if (line == "\r") {
      break;
    }
  }

  String line = client.readStringUntil('\n');
//Serial.println(line);

}

void setup() {
  M5.begin(false);
  //M5.dis.clear();
  Serial.begin(115200);
  Serial1.begin(115200,SERIAL_8N1, 21,25);
  Serial.println("\nStart");
  setenv("TZ","JST-9",1);
  wifiConnect();
  bool s;
  s = ambient.begin(channelId, writeKey, &aclient); 
  Serial.println(s);
}

void loop() {
  static char message[300], datetime[30];
  while (Serial1.available()) {
      int c = Serial1.read();
      parse_ascii << char_t(c);
      if (parse_ascii) {
        getLocalTime(&timeInfo);
        sprintf(datetime, "Time:%04d/%02d/%02d %02d:%02d:%02d",timeInfo.tm_year+1900, timeInfo.tm_mon+1, timeInfo.tm_mday, timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
        auto&& pkt = newTwePacket(parse_ascii.get_payload());
        E_PKT pkt_typ = identify_packet_type(pkt);
        if (pkt_typ == E_PKT::PKT_PAL) {
          auto&& pal = refTwePacketPal(pkt);
              
          if (pal.u8palpcb == E_PAL_PCB::MAG) {
            Serial.println("MAG");
            PalMag obj = pal.get_PalMag();
            char *status;
            switch(obj.u8MagStat) {
              case 0:
                status = "Open";
                sprintf(message,"Door open, %s\n", datetime );
                send(message);
                break;
              default:
                status = "Close";
            }
            sprintf(message, "Magnet PAL notify: %s, BAT %d", status, obj.u16Volt);
            Serial.println(message);
          } else if (pal.u8palpcb == E_PAL_PCB::AMB) {
            Serial.println("AMB");
            PalAmb obj = pal.get_PalAmb();
            sprintf(message, "Ambient PAL notify: Temp %3.1f, Hum %3.1f, Lux %3d, BAT %d", obj.i16Temp / 100.0 , obj.u16Humd / 100.0, obj.u32Lumi, obj.u16Volt);
            Serial.println(message);
            ambient.set(1, obj.i16Temp/100.0);
            ambient.set(2, obj.u16Humd/100.0);
            ambient.set(3, (int)(obj.u32Lumi));
            ambient.set(4, obj.u16Volt);
            bool s;
            s = ambient.send();
            Serial.println(s);
          } else if (pal.u8palpcb == E_PAL_PCB::MOT) {
            Serial.println("MOT");
            PalMot obj = pal.get_PalMot();
            sprintf(message,"Motion PAL notify: Samples: %d, x[0]: %d, y[0]: %d, z[0]: %d, BAT %d", obj.u8samples, obj.i16X[0], obj.i16Y[0], obj.i16Z[0], obj.u16Volt);
            Serial.println(message);
            int sumZ=0;
            for(int i=0; i<obj.u8samples; i++){
              sumZ += obj.i16Z[i];
            }
            if(sumZ > 0) {
              sprintf(message, "Milkbox Open, %s\n", datetime);
              send(message);
            }
          } else {
            Serial.println("UNKNOWN PAL");
        }
      }
    }
  }
}
