/* v1.0.1
 * COMPATIBLE:
 *  - ARDUINO UNO
 *  - ARDUINO Pro Mini 5V
 * NOTAS:
 * - No olvidar cambiar en VirtualWire.h el valor de VW_MAX_MESSAGE_LEN a 50
 */

#include <avr/wdt.h>

#define SERIALPRINT   // Para sacar información por consola (mejor desactivar para producción)
#define SENAL_LED  // Si queremos que se encienda el led_pin
//#define LEONARDO

// lED
const int led_pin = 4;  // No utilizar el PIN-13 pues es usado por el Ethernet (SCK)

// Reinicios programados
long milisecIniciales = 0;
long reinicoCadaSegundos = 60 * 30; // Configurar (60*30 = cada 30min)

// ************ RF ************

#include <VirtualWire.h>
byte message[VW_MAX_MESSAGE_LEN]; // a buffer to store the incoming messages
byte messageLength = VW_MAX_MESSAGE_LEN; // the size of the message
const int RX_DIO_Pin = 9;  // usar 2 ó 9 por diseño

// ************ ETH ************

#include <EtherCard.h>

// ethernet interface mac address, must be unique on the LAN. Macs FIEB AA, AB, AC, AF
static byte mymac[] = { 0x74, 0x69, 0x69, 0x2D, 0x30, 0xB8 };
byte Ethernet::buffer[700];
static uint32_t timer;
const char website[] PROGMEM = "b.conectate.es";

// EmonCMS
char EMONCMS_Key[] = "09f0d0ae02cd22e646bd7c171cb15d54";

int REQ = 1;

void setup() {
  #ifdef SERIALPRINT
    Serial.begin(115200);
  #endif

  #ifdef SENAL_LED
    pinMode(led_pin, OUTPUT);
  #endif

  // RF
  vw_set_rx_pin(RX_DIO_Pin);
  vw_set_ptt_inverted(true);
  vw_setup(2000); // Bits per sec
  vw_rx_start(); // Start the receiver

  /*** Para reiniciar cada X segundos ***/
  //clear all flags
  MCUSR = 0;
  /* Write logical one to WDCE and WDE */
  /* Keep old prescaler setting to prevent unintentional time-out */
  WDTCSR |= _BV(WDCE) | _BV(WDE);
  WDTCSR = 0;

  milisecIniciales = millis();

  Serial.print(F("Iniciando...\n"));

  // Ethernet. Leonardo usa PIN 10, resto de Arduinos PIN 8
  #ifdef LEONARDO
  if (ether.begin(sizeof Ethernet::buffer, mymac, 10) == 0)
    Serial.println(F("ERROR de acceso al controlador Ethernet!"));
  #else
  if (ether.begin(sizeof Ethernet::buffer, mymac) == 0)
    Serial.println(F("ERROR de acceso al controlador Ethernet!"));
  #endif

  
  if (!ether.dhcpSetup()) {
    Serial.print(F("ERROR DHCP!"));
    software_Reset();
  }

  ether.printIp("IP:  ", ether.myip);
  ether.printIp("GW:  ", ether.gwip);
  ether.printIp("DNS: ", ether.dnsip);

  if (!ether.dnsLookup(website)) {
    Serial.print(F("ERROR DNS!\nReiniciando\n"));
    software_Reset();
  }

  ether.printIp("SRV: ", ether.hisip);
  Serial.println((""));

  #ifdef SENAL_LED
    parpadeo(led_pin, 10, 100); // 2 segundos
  #endif
}

void loop() {
  tenemosQueReiniciar();

  uint8_t message[VW_MAX_MESSAGE_LEN];
  uint8_t messageLength = VW_MAX_MESSAGE_LEN;
  
  char linea[100] = "";
  
  ether.packetLoop(ether.packetReceive());
    if (vw_get_message(message, &messageLength))  {
      vw_rx_stop();
      REQ = 0;
      strcpy(linea, "");
  
      #ifdef SERIALPRINT
        Serial.print(F("Recibido: "));
      #endif SERIALPRINT
      for (int i = 0; i < messageLength; i++) {
        sprintf(linea, "%s%c\0", linea, message[i]);
        // Serial.write(message[i]);
      }
  
      #ifdef SERIALPRINT
        Serial.println(linea); 
      #endif
      realizarLlamada(linea, ' ');
      #ifdef SENAL_LED
        parpadeo(led_pin, 1, 10);  // Hay q darle poco tiempo para que pueda responder a muchas peticiones
      #endif
      delay(2221);
      vw_rx_start();
    }
    
  
  
}

static void my_callback (byte status, word off, word len) {
  Serial.println(">>>> REQ");
  Ethernet::buffer[off + 300] = 0;
  return;
  // Serial.print((const char*) Ethernet::buffer + off);
  // Serial.println(">>>> REQ");
}

void tenemosQueReiniciar() {
  unsigned long milisecActuales = millis();
  if (milisecActuales - milisecIniciales > reinicoCadaSegundos * 1000) {
    software_Reset();
  }
}

void software_Reset() {
  // http://www.xappsoftware.com/wordpress/2013/06/24/three-ways-to-reset-an-arduino-board-by-code/
  // http://arduino.stackexchange.com/questions/1477/reset-an-arduino-uno-by-an-command-software
  #ifdef SERIALPRINT
    Serial.print(F("Reinicio programado.\n"));
  #endif

  #ifdef LEONARDO
    milisecIniciales = millis();
    Serial.print(F("En Leonardo no se reinicia!!\n"));
    return;
  #endif

  #ifndef LEONARDO
    delay(1000);
    asm volatile ("  jmp 0");
  #endif

  wdt_enable(WDTO_120MS); // turn on the WatchDog and don't stroke it.
  for (;;) {
    // do nothing and wait for the eventual...
  }
}

int getLongitudCaracteresUsadosEnArray(char cadena[]) {
  char *cadena_ptr = &cadena[0];
  return strlen(cadena_ptr);
}

int contieneCaracter(char cadena[], char caracter) {
  int longitud = getLongitudCaracteresUsadosEnArray(cadena);

  for(int t = 0; t < longitud; t++) {
    if(cadena[t]==caracter) {
      return true;
    }
  }

  return false;
}

void realizarLlamada(char mensaje[], char separador) {
  // printf("Recibido: %s\n", mensaje);
  char id[20] = "", valor[100] = "";
  int _id = 0, _valor = 0;
  int numSeparadoresEncontrados = 0;
  bool todoCorrecto = false;
  char paguinaWeb[150] = ""; // "AM2 196 3893" || "AM2 {t:32.3,h:23.2,v:3222}"

  bool esNodo = contieneCaracter(mensaje, '{');
  int longitudMensaje = getLongitudCaracteresUsadosEnArray(mensaje);

  if (!esNodo) {
    for (int t = 0; t < longitudMensaje; t++) {
      // Tres parámetros: "CA01 4 23". El primero no sirve para nada
      if ((numSeparadoresEncontrados == 1) && (mensaje[t] != separador)) {  // estamos leyendo el ID/Nodo
        id[_id] = mensaje[t];
        _id++;
      }
      if ((numSeparadoresEncontrados == 2) && (mensaje[t] != separador)) {  // estamos leyendo el valor
        valor[_valor] = mensaje[t];
        _valor++;
      }
      if ((mensaje[t] == separador) || (t == longitudMensaje - 1)) {
        numSeparadoresEncontrados++;
      }
      if (numSeparadoresEncontrados == 3) {
        // Estamos ante otro ID+Valor
        valor[_valor] = '\0';
        id[_id] = '\0';
        sprintf(paguinaWeb, "insert.json?id=%s&value=%s&apikey=%s", id, valor, EMONCMS_Key);
        todoCorrecto = true;
        break;
      }
    }
  } else {
    // Dos parámetros: "AA00 {t:21.1,h:71,v:5120}"
    for (int t = 0; t < longitudMensaje; t++) {
      if ((numSeparadoresEncontrados == 0) && (mensaje[t] != separador)) {  // estamos leyendo el ID/Nodo
        id[_id] = mensaje[t];
        _id++;
      }
      if ((numSeparadoresEncontrados == 1) && (mensaje[t] != separador)) {  // estamos leyendo el valor
        valor[_valor] = mensaje[t];
        _valor++;
      }
      if ((mensaje[t] == separador) || (t == longitudMensaje - 1)) {
        numSeparadoresEncontrados++;
      }
      if (numSeparadoresEncontrados == 2) {
        // FIN
        valor[_valor] = '\0';
        id[_id] = '\0';
        sprintf(paguinaWeb, "post.json?node=%s&apikey=%s&json=%s", id, EMONCMS_Key, valor);
        if (contieneCaracter(valor, '}'))
          todoCorrecto = true;
        break;
      }
    }
  }

  if (todoCorrecto) {
    #ifdef SERIALPRINT
      Serial.println(paguinaWeb);
    #endif
      
    if (!esNodo)
      ether.browseUrl(PSTR("/emoncms/feed/"), paguinaWeb, website, my_callback);
    else
      ether.browseUrl(PSTR("/emoncms/input/"), paguinaWeb, website, my_callback);
      
  }
}

void parpadeo(int pinLed, int veces, int tiempo) {
  for (int t = 0; t < veces; t++) {
    digitalWrite(pinLed, HIGH);
    delay(tiempo);
    digitalWrite(pinLed, LOW);
    delay(tiempo);
  }
}
