#include "Arduino.h"
#ifndef USE_SDIO
  #define USE_SDIO 1 // change to 1 if using SDIO 
#endif

#define NCH 4
#define NBUF_ACQ 128*NCH

#include "MTP.h"
#include "usb1_mtp.h"

MTPStorage_SD storage;
MTPD       mtpd(&storage);


void logg(uint32_t del, const char *txt);

int16_t state;
int16_t do_logger(int16_t state);
int16_t do_menu(int16_t state);
int16_t check_filing(int16_t state);

void acq_init(int32_t fsamp);
int16_t acq_check(int16_t state);

void setup()
{ 
  while(!Serial && millis()<3000); 
  usb_mtp_configure();
  if(!Storage_init()) { Serial.println("No storage"); while(1);};

  Serial.println("MTP logger");

  #if USE_SDIO==1
    pinMode(13,OUTPUT);
  #endif
  acq_init(192000);
  state=-1;
}

uint32_t loop_count=0;
void loop()
{ loop_count++;
  state = do_menu(state);
  state = acq_check(state);
  state = check_filing(state);
  //
  if(state<0)
    mtpd.loop();
  else
    state=do_logger(state);

  logg(1000,"loop");
  //asm("wfi"); // may wait forever on T4.x
}

/*************************** Circular Buffer ********************/
  #define HAVE_PSRAM 1
  #if HAVE_PSRAM==1
    #define MAXBUF (1000) // 3000 kB   // < 5461 for 16 MByte PSRAM

    extern "C" uint8_t external_psram_size;
    uint8_t size = external_psram_size;
    uint32_t *memory_begin = (uint32_t *)(0x70000000);

    uint32_t *data_buffer = memory_begin;
  #else
    #if defined(ARDUINO_TEENSY41)
      #define MAXBUF (46)           // 138 kB
    #elif defined(ARDUINO_TEENSY40)
      #define MAXBUF (46)           // 138 kB
    #elif defined(__MK66FX1M0__)
      #define MAXBUF (46)           // 138 kB
    #elif defined(__MK20DX256__)
      #define MAXBUF (12)           // 36 kB
    #endif
    uint32_t data_buffer[MAXBUF*NBUF_ACQ];
  #endif

static uint16_t front_ = 0, rear_ = 0;
uint16_t getCount () { if(front_ >= rear_) return front_ - rear_; return front_+ MAXBUF -rear_; }
uint16_t maxCount=0;

uint16_t pushData(uint32_t * src)
{ uint16_t f =front_ + 1;
  if(f >= MAXBUF) f=0;
  if(f == rear_) return 0;

  uint32_t *ptr= data_buffer+f*NBUF_ACQ;
  memcpy(ptr,src,NBUF_ACQ*4);
  front_ = f;
  //
  uint16_t count;
  count = (front_ >= rear_) ? (front_ - rear_) : front_+ (MAXBUF -rear_) ;
  if(count>maxCount) maxCount=count;
  //
  return 1;
}

uint16_t pullData(uint32_t * dst, uint16_t ndbl)
{ uint16_t r = (rear_/ndbl) ;
  if(r == (front_/ndbl)) return 0;
  if(++r >= (MAXBUF/ndbl)) r=0;
  uint32_t *ptr= data_buffer + r*ndbl*NBUF_ACQ;
  memcpy(dst,ptr,ndbl*NBUF_ACQ*4);
  rear_ = r*ndbl;
  return 1;
}

/*************************** Filing *****************************/
int16_t file_open(void);
int16_t file_writeHeader(void);
int16_t file_writeData(void *diskBuffer, uint32_t ndbl);
int16_t file_close(void);
#define NDBL 1
#define NBUF_DISK (NDBL*NBUF_ACQ)
uint32_t diskBuffer[NBUF_DISK];

int16_t do_logger(int16_t state)
{
  if(pullData(diskBuffer,NDBL))
  {
    if(state==0)
    { // acquisition is running, need to open file
      if(!file_open()) return -1;
      state=1;
    }
    if(state==1)
    { // file just opended, need to write header
      if(!file_writeHeader()) return -1;
      state=2;
      
    }
    if(state>=2)
    { // write data to disk
      if(!file_writeData(diskBuffer,NBUF_DISK*4)) return -1;
    }
  }

  if(state==3)
  { // close file, but continue acquisition
    if(!file_close()) return -1;
    state=0;
    
  }
  if(state==4)
  { // close file and stop acquisition
    if(!file_close()) return -1;
    state=-1;
    
  }
  return state;
}

/******************** Menu ***************************/
void do_menu1(void);
void do_menu2(void);
void do_menu3(void);

int16_t do_menu(int16_t state)
{ // check Serial input
  if(!Serial.available()) return state;
  char cc = Serial.read();
  switch(cc)
  {
    case 's': // start acquisition
      if(state>=0) return state;
      state=0;
      break;
    case 'q': // stop acquisition
      if(state<0) return state;
      state=4;
      break;
    case '?': // get parameters
      do_menu1();
      break;
    case '!': // set parameters
      if(state>=0) return state;
      do_menu2();
      break;
    case ':': // misc commands
      if(state>=0) return state;
      do_menu3();
      break;
    default:
      break;
  }
  return state;
}

/************ Basic File System Interface *************************/
extern SdFs sd;
static FsFile mfile;
char header[512];

void makeHeader(char *header);
int16_t makeFilename(char *filename);
int16_t checkPath(char *filename);

int16_t file_open(void)
{ char filename[80];
  if(!makeFilename(filename)) return 0;
  if(!checkPath(filename)) return 0;
  mfile = sd.open(filename,FILE_WRITE);
  return mfile.isOpen();
}
int16_t file_writeHeader(void)
{ if(!mfile.isOpen()) return 0;
  makeHeader(header);
  size_t nb = mfile.write(header,512);
  return (nb==512);
}
int16_t file_writeData(void *diskBuffer, uint32_t nd)
{
  if(!mfile.isOpen()) return 0;
  uint32_t nb = mfile.write(diskBuffer,nd);
  return (nb==nd);
}
int16_t file_close(void)
{ return mfile.close();
}

/*
 * Custom Implementation
 * 
 */

/************* Menu implementation ******************************/
void do_menu1(void)
{  // get parameters
}
void do_menu2(void)
{  // set parameters
}
void do_menu3(void)
{  // misc commands
}

extern uint32_t loop_count, acq_count, acq_fail;
extern uint16_t maxCount;
/**************** Online logging *******************************/
void logg(uint32_t del, const char *txt)
{ static uint32_t to;
  if(millis()-to > del)
  {
    Serial.printf("%s: %6d %4d %4d %4d\n",txt,loop_count, acq_count, acq_fail,maxCount); 
    loop_count=0;
    acq_count=0;
    acq_fail=0;
    maxCount=0;
    #if USE_SDIO==1
        digitalWriteFast(13,!digitalReadFast(13));
    #endif
    to=millis();
  }
}
/****************** File Utilities *****************************/
void makeHeader(char *header)
{
  memset(header,0,512);
}

int16_t makeFilename(char *filename)
{
  uint32_t tt = rtc_get();
  int hh,mm,ss;
  int dd;
  ss= tt % 60; tt /= 60;
  mm= tt % 60; tt /= 60;
  hh= tt % 24; tt /= 24;
  dd= tt;
  sprintf(filename,"/%d/%02d_%02d_%02d.raw",dd,hh,mm,ss);
  Serial.println(filename);
  return 1;
}

int16_t checkPath(char *filename)
{
  int ln=strlen(filename);
  int i1=-1;
  for(int ii=0;ii<ln;ii++) if(filename[ii]=='/') i1=ii;
  if(i1<0) return 1; // no path
  filename[i1]=0;
  if(!sd.exists(filename))
  { Serial.println(filename); 
    if(!sd.mkdir(filename)) return 0;
  }

  filename[i1]='/';
  return 1;
}

uint32_t t_on = 60;
int16_t check_filing(int16_t state)
{
  static uint32_t to;
  if(state==2)
  {
    uint32_t tt = rtc_get();
    uint32_t dt = tt % t_on;
    if(dt<to) state = 3;
    to = dt;
  }
  return state;
}

/****************** Data Acquisition (dummy example) *****************************/
#include "IntervalTimer.h"

IntervalTimer t1;

uint32_t acq_period=1000;
int16_t acq_state=-1;
void acq_isr(void);

void acq_init(int32_t fsamp)
{ acq_period=128'000'000/fsamp;
  acq_state=0;
}

void acq_start(void)
{ if(acq_state) return;
  t1.begin(acq_isr, acq_period);
  acq_state=1;
}

void acq_stop(void)
{ if(acq_state<=0) return;
  t1.end();
  acq_state=0;
}

uint32_t acq_count=0;
uint32_t acq_fail=0;
uint32_t acq_buffer[NBUF_ACQ];

void acq_isr(void)
{ acq_count++;
  for(int ii=0;ii<128;ii++) acq_buffer[ii*NCH]=acq_count;
  if(!pushData(acq_buffer)) acq_fail++;
}

int16_t acq_check(int16_t state)
{ if(!state)
  { // start acquisition
    acq_start();
  }
  if(state>3)
  { // stop acquisition
    acq_stop();
  }
  return state;
}
