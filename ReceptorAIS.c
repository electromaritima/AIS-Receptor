#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Dns.h>
#include <Controllino.h>

// =====================================================
// SERVIDOR REAL NETTRA
// =====================================================

const char SERVER_HOST[] = "ais.nettra.tech";
const uint16_t SERVER_PORT = 6000;


// =====================================================
// MONITOREO BROADCAST
// =====================================================

// Todos los equipos de la red 192.168.0.x
// pueden recibir esta copia.
IPAddress BROADCAST_IP(192, 168, 0, 255);

// Puerto exclusivo para monitoreo local
const uint16_t BROADCAST_PORT = 6001;


// =====================================================
// CONFIGURACION ETHERNET
// =====================================================

// MAC del Controlino.
// La IP sera entregada por DHCP.
byte mac[] = {
  0x02, 0x45, 0x4D, 0x55, 0x01, 0x01
};

// Puerto UDP local/origen del Controlino
const uint16_t LOCAL_UDP_PORT = 50001;


// =====================================================
// TIEMPOS
// =====================================================

// Si Nettra no puede resolverse,
// intentar nuevamente cada 60 segundos.
const unsigned long DNS_RETRY_INTERVAL = 60000;

// Estadisticas cada 5 segundos
const unsigned long STATS_INTERVAL = 5000;


// =====================================================
// OBJETOS DE RED
// =====================================================

EthernetUDP udp;
DNSClient dnsClient;


// =====================================================
// IP DE NETTRA
// =====================================================

IPAddress nettraIP;

bool nettraDisponible = false;


// =====================================================
// BUFFER NMEA
// =====================================================

char bufferNMEA[256];
uint16_t pos = 0;


// =====================================================
// CONTADORES
// =====================================================

unsigned long contadorRX = 0;

unsigned long contadorBroadcast = 0;
unsigned long contadorNettra = 0;

unsigned long erroresBroadcast = 0;
unsigned long erroresNettra = 0;

unsigned long ultimoIntentoDNS = 0;
unsigned long ultimoReporte = 0;


// =====================================================
// MOSTRAR IP
// =====================================================

void printIP(IPAddress ip)
{
  Serial.print(ip[0]);
  Serial.print(".");
  Serial.print(ip[1]);
  Serial.print(".");
  Serial.print(ip[2]);
  Serial.print(".");
  Serial.print(ip[3]);
}


// =====================================================
// RESOLVER ais.nettra.tech
// =====================================================

void resolverNettra()
{
  Serial.println();
  Serial.print("Resolviendo ");
  Serial.print(SERVER_HOST);
  Serial.print(" ... ");

  // Utilizamos el DNS entregado por DHCP
  dnsClient.begin(Ethernet.dnsServerIP());

  int resultado =
    dnsClient.getHostByName(SERVER_HOST, nettraIP);

  if (resultado == 1)
  {
    nettraDisponible = true;

    Serial.print("OK -> ");
    printIP(nettraIP);
    Serial.println();
  }
  else
  {
    nettraDisponible = false;

    Serial.println("SIN INTERNET / DNS NO DISPONIBLE");
  }
}


// =====================================================
// ENVIAR AIS
// =====================================================

void enviarAIS(char *data, uint16_t length)
{
  if (length == 0)
    return;

  contadorRX++;


  // ===================================================
  // 1 - BROADCAST LOCAL
  // ===================================================
  //
  // Esto permite que cualquier PC de la LAN pueda
  // ver las sentencias con Wireshark.
  //
  // Destino:
  //
  // 192.168.0.255 : 6001
  //
  // ===================================================

  if (udp.beginPacket(BROADCAST_IP, BROADCAST_PORT))
  {
    udp.write((uint8_t *)data, length);

    if (udp.endPacket())
    {
      contadorBroadcast++;
    }
    else
    {
      erroresBroadcast++;
    }
  }
  else
  {
    erroresBroadcast++;
  }


  // ===================================================
  // 2 - ENVIO REAL A NETTRA
  // ===================================================

  if (nettraDisponible)
  {
    // Usamos la IP previamente resuelta.
    // NO hacemos una consulta DNS por cada trama AIS.

    if (udp.beginPacket(nettraIP, SERVER_PORT))
    {
      udp.write((uint8_t *)data, length);

      if (udp.endPacket())
      {
        contadorNettra++;
      }
      else
      {
        erroresNettra++;
      }
    }
    else
    {
      erroresNettra++;
    }
  }


  // ===================================================
  // MOSTRAR NMEA EN ARDUINO IDE
  // ===================================================

  // Mostramos la sentencia recibida.
  // El Monitor Serie debe estar en 115200 baud.

  Serial.write((uint8_t *)data, length);
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  // ===================================================
  // MONITOR USB
  // ===================================================

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" CONTROLINO AIS GATEWAY");
  Serial.println(" COMAR R200U");
  Serial.println("========================================");


  // ===================================================
  // RS485 / NMEA AIS
  // ===================================================

  Controllino_RS485Init(38400);
  Controllino_RS485RxEnable();

  Serial.println("AIS RS485 : 38400 baud");


  // ===================================================
  // ETHERNET DHCP
  // ===================================================

  Ethernet.init(CONTROLLINO_ETHERNET_CHIP_SELECT);

  Serial.println();
  Serial.println("Solicitando IP por DHCP...");

  if (Ethernet.begin(mac) == 0)
  {
    Serial.println("*** ERROR DHCP ***");
    Serial.println("No se obtuvo direccion IP.");
  }
  else
  {
    Serial.println("*** DHCP OK ***");

    Serial.print("IP Controlino : ");
    printIP(Ethernet.localIP());
    Serial.println();

    Serial.print("Gateway       : ");
    printIP(Ethernet.gatewayIP());
    Serial.println();

    Serial.print("DNS           : ");
    printIP(Ethernet.dnsServerIP());
    Serial.println();

    Serial.print("Mascara       : ");
    printIP(Ethernet.subnetMask());
    Serial.println();
  }


  // ===================================================
  // UDP
  // ===================================================

  Serial.println();

  if (udp.begin(LOCAL_UDP_PORT))
  {
    Serial.println("*** UDP OK ***");

    Serial.print("Puerto origen : ");
    Serial.println(LOCAL_UDP_PORT);

    Serial.print("Broadcast     : ");
    printIP(BROADCAST_IP);
    Serial.print(":");
    Serial.println(BROADCAST_PORT);

    Serial.print("NETTRA        : ");
    Serial.print(SERVER_HOST);
    Serial.print(":");
    Serial.println(SERVER_PORT);
  }
  else
  {
    Serial.println("*** ERROR INICIALIZANDO UDP ***");
  }


  // ===================================================
  // PRIMER INTENTO DNS
  // ===================================================

  resolverNettra();

  ultimoIntentoDNS = millis();


  Serial.println();
  Serial.println("========================================");
  Serial.println(" ESPERANDO AIS...");
  Serial.println("========================================");
  Serial.println();
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  // ===================================================
  // MANTENER DHCP
  // ===================================================

  Ethernet.maintain();


  // ===================================================
  // REINTENTAR DNS
  // ===================================================

  if (!nettraDisponible)
  {
    if (millis() - ultimoIntentoDNS >= DNS_RETRY_INTERVAL)
    {
      ultimoIntentoDNS = millis();

      resolverNettra();
    }
  }


  // ===================================================
  // LEER AIS DESDE R200U
  // ===================================================

  while (Serial3.available())
  {
    char c = Serial3.read();


    // =================================================
    // ESPERAR COMIENZO DE SENTENCIA
    // =================================================

    if (pos == 0)
    {
      // AIS NMEA comienza con !
      if (c != '!')
        continue;
    }


    // =================================================
    // GUARDAR CARACTER
    // =================================================

    if (pos < sizeof(bufferNMEA) - 1)
    {
      bufferNMEA[pos++] = c;
    }
    else
    {
      // Seguridad en caso de trama anormal
      pos = 0;
      continue;
    }


    // =================================================
    // FIN DE SENTENCIA
    // =================================================

    if (c == '\n')
    {
      bufferNMEA[pos] = '\0';


      // =================================================
      // ACEPTAR SOLO AIS
      // =================================================

      if (
        strncmp(bufferNMEA, "!AIVDM", 6) == 0 ||
        strncmp(bufferNMEA, "!AIVDO", 6) == 0
      )
      {
        enviarAIS(bufferNMEA, pos);
      }


      // Preparar siguiente sentencia
      pos = 0;
    }
  }


  // ===================================================
  // ESTADISTICAS
  // ===================================================

  if (millis() - ultimoReporte >= STATS_INTERVAL)
  {
    ultimoReporte = millis();

    Serial.println();

    Serial.print("[STAT] AIS RX=");
    Serial.print(contadorRX);

    Serial.print(" | BROADCAST=");
    Serial.print(contadorBroadcast);

    Serial.print(" | NETTRA=");
    Serial.print(contadorNettra);

    Serial.print(" | ERR BC=");
    Serial.print(erroresBroadcast);

    Serial.print(" | ERR NET=");
    Serial.println(erroresNettra);
  }
}