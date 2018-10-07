//handle communications
#include "LoRaWAN.h"
#include "TimerMillis.h"

// TTN fair usage policy guideline
// An average of 30 seconds uplink time on air, per day, per device. 
// 20 messages per day at SF12, 500 messages per day at SF7, for 12byte payload
// At most 10 downlink messages per day, including the ACKs for confirmed uplinks.

// Configure the keys here, node the DevEUI is acquired from the module, but you can manually override
const char *appEui  = "70B3D57ED0010F91";
const char *appKey  = "50BC4179C8259B9D9B9C05FCBD80A7FD";
//const char *devEui  = "enter here"; //uncomment if manual
char devEui[32]; // uncomment if not manual

TimerMillis transmitTimer; //timer for transmission events

#define PACKET_SIZE 12
typedef struct sensorData_t{
  uint8_t   stat;
  int8_t    t1;
  uint8_t   t01;
  int8_t    h1;
  uint8_t   h01;
  uint16_t  ap;
  int8_t    acc;
  uint16_t  vdd;
  int8_t    tc1;
  uint8_t   tc01;
};

typedef union lorawanPacket_t{
  sensorData_t sensor;
  byte bytes[sizeof(sensorData_t)];
}__attribute__((packed));

lorawanPacket_t packet;

//Implement automatic adjustment to send data at TTN fair policy rate
//Slightly cheats after reboot for first 100 uplinks, such that ADR can kick in, testing only
//TODO: prototype implementation, not correctly implemented

long ttn_fair_policy(){
  //int payload_length = 13 + sizeof(sensorData_t); //header is 13 bytes
   //see calculation https://docs.google.com/spreadsheets/d/1QvcKsGeTTPpr9icj4XkKXq4r2zTc2j0gsHLrnplzM3I/edit#gid=0
  //TODO: insert proper calculation, now assuming 12 byte payload
  //TODO: for proper calculation daily airtime should be monitored, now assuming DR does not change often/daily
  int daily_uplinks = 486; // default value for SF7
  switch (LoRaWAN.getDataRate()) {
    case 0: //SF12
      daily_uplinks = 20;
      break;
    case 1://SF11
      daily_uplinks = 36;
      break;
    case 2://SF10
      daily_uplinks = 72;
      break;
    case 3://SF9
      daily_uplinks = 145;
      break;
    case 4://SF8
      daily_uplinks = 265;
      break;
    case 5://SF7
      daily_uplinks = 486;
      break;
    default:
      daily_uplinks = 2880;//every 30s
  }
  int check_period = daily_uplinks/10 ; //in number of uplinks, 10 downlinks daliy limit

  //testing only, force quicker convergence to final datarate
  //check if more then 100 packets have been sent, ADR should have kicked in
  if(LoRaWAN.getUpLinkCounter()<100){
    daily_uplinks = 2880;//every 30s
    check_period = 10; //every 10 uplinks
  }
  
  long transmit_delay = 24*3600/daily_uplinks;

  LoRaWAN.setLinkCheckLimit(check_period); // number of uplinks link check is sent, 10 for experimenting
  LoRaWAN.setLinkCheckDelay(4); // number of uplinks waiting for an answer, 2 for experimenting, 4 otherwise
  LoRaWAN.setLinkCheckThreshold(4); // number of times link check fails to assert link failed, 1 for experimenting, 4 otherwise
    
  #ifdef debug
    serial_debug.print("TTN fair policy daily uplinks: ");
    serial_debug.print(daily_uplinks);
    serial_debug.println("");
    serial_debug.print("link check every  ");
    serial_debug.print(check_period);
    serial_debug.println(" uplinks");
  #endif
  
  return transmit_delay;
}

void comms_setup( void )
{
    //Get the device ID and print
    LoRaWAN.getDevEui(devEui, 18); //comment if manual override
    
    #ifdef debug
      serial_debug.print("STM32L0 Device EUI = "); 
      serial_debug.println(devEui); 
    #endif

    //Configure lora parameters
    LoRaWAN.begin(EU868);
    LoRaWAN.addChannel(1, 868300000, 0, 6);
    
    LoRaWAN.setDutyCycle(false); // must be true except for development in confined environments
    LoRaWAN.setAntennaGain(0.0); // must be equal to the installed antenna
    LoRaWAN.setADR(true); // Kicks in after 64 received packets
    //LoRaWAN.setLinkCheckLimit(5); // number of uplinks link check is sent, 5 for experimenting, 20 otherwise
    //LoRaWAN.setLinkCheckDelay(2); // number of uplinks waiting for an answer, 2 for experimenting, 4 otherwise
    //LoRaWAN.setLinkCheckThreshold(1); // number of times link check fails to assert link failed, 1 for experimenting, 4 otherwise
    // see examples/LoRaWAN_Disconnect/LoRaWAN_Disconnect.ino
    
    LoRaWAN.onJoin(joinCallback);
    LoRaWAN.onLinkCheck(checkCallback);
    LoRaWAN.onTransmit(doneCallback);
    LoRaWAN.onReceive(receiveCallback);
    
    LoRaWAN.joinOTAA(appEui, appKey, devEui);
}

void transmitCallback(void)
{
  STM32L0.wakeup();
  comms_transmit_flag = true;
}
void comms_transmit(void)
{
  //configure timer
  long send_period = ttn_fair_policy();
  transmitTimer.start(transmitCallback, 0, send_period); // schedule a transmission
  if (!LoRaWAN.busy())
  {
    if (!LoRaWAN.linkGateways())
    {
      transmitTimer.stop();
      #ifdef debug
        serial_debug.println("REJOIN( )");
      #endif
        LoRaWAN.rejoinOTAA();
    }
    
    if (LoRaWAN.joined())
    {
      #ifdef debug
        serial_debug.print("TRANSMIT( ");
        serial_debug.print("TimeOnAir: ");
        serial_debug.print(LoRaWAN.getTimeOnAir());
        serial_debug.print(", NextTxTime: ");
        serial_debug.print(LoRaWAN.getNextTxTime());
        serial_debug.print(", MaxPayloadSize: ");
        serial_debug.print(LoRaWAN.getMaxPayloadSize());
        serial_debug.print(", DR: ");
        serial_debug.print(LoRaWAN.getDataRate());
        serial_debug.print(", TxPower: ");
        serial_debug.print(LoRaWAN.getTxPower(), 1);
        serial_debug.print("dbm, UpLinkCounter: ");
        serial_debug.print(LoRaWAN.getUpLinkCounter());
        serial_debug.print(", DownLinkCounter: ");
        serial_debug.print(LoRaWAN.getDownLinkCounter());
        serial_debug.println(" )");
      #endif
      //read sensors and then send data
      read_sensors();
      // int sendPacket(uint8_t port, const uint8_t *buffer, size_t size, bool confirmed = false);
      LoRaWAN.sendPacket(2, packet.bytes, sizeof(sensorData_t), false);
    }
  }
}

// Callback on Join failed/success
void joinCallback(void)
{
    if (LoRaWAN.joined())
    {
      #ifdef debug
        serial_debug.println("JOINED");
      #endif
      //force trigger transmitt callback
      transmitCallback();
    }
    else
    {
      #ifdef debug
        serial_debug.println("REJOIN( )");
      #endif
      LoRaWAN.rejoinOTAA();
    }
}

// Link check callback, useful for ADR debugging
void checkCallback(void)
{
  #ifdef debug
    serial_debug.print("CHECK( ");
    serial_debug.print("RSSI: ");
    serial_debug.print(LoRaWAN.lastRSSI());
    serial_debug.print(", SNR: ");
    serial_debug.print(LoRaWAN.lastSNR());
    serial_debug.print(", Margin: ");
    serial_debug.print(LoRaWAN.linkMargin());
    serial_debug.print(", Gateways: ");
    serial_debug.print(LoRaWAN.linkGateways());
    serial_debug.println(" )");
  #endif
}

// callback upon receiving data
void receiveCallback(void)
{
  #ifdef debug
    serial_debug.print("RECEIVE( ");
    serial_debug.print("RSSI: ");
    serial_debug.print(LoRaWAN.lastRSSI());
    serial_debug.print(", SNR: ");
    serial_debug.print(LoRaWAN.lastSNR());
  #endif
  if (LoRaWAN.parsePacket())
  {
    uint32_t size;
    uint8_t data[256];

    size = LoRaWAN.read(&data[0], sizeof(data));

    if (size)
    {
      data[size] = '\0';
      #ifdef debug
        serial_debug.print(", PORT: ");
        serial_debug.print(LoRaWAN.remotePort());
        serial_debug.print(", DATA: \"");
        serial_debug.print((const char*)&data[0]);
        serial_debug.println("\"");
      #endif
    }
  }
  #ifdef debug
    serial_debug.println(" )");
  #endif
}

//callback on link lost, handle rejoining schedule here
void doneCallback(void)
{
  if (!LoRaWAN.linkGateways())
  {
    #ifdef debug
      serial_debug.println("DISCONNECTED");
    #endif
  }
}