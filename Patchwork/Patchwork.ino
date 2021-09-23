#include "Bounce2.h"
#include "Encoder.h"

#define MSG(v) {Serial.println(v);}
#define DBG(v) {Serial.print(#v " = ");Serial.println(v);}
#define DBG2(s,v) {Serial.print(#s " = ");Serial.println(v);}
#define DBG3(s,v) {Serial.print(#s ", " #v " = ");Serial.println(v);}
#define MSGn(v) {Serial.print(v);}
#define DBGn(v) {Serial.print(#v " = ");Serial.print(v);Serial.print(" ");}
#define DBG2n(s,v) {Serial.print(#s " = ");Serial.print(v);Serial.print(" ");}
#define DBG3n(s,v) {Serial.print(#s ", " #v " = ");Serial.print(v);Serial.print(" ");}
#define DBGLINE {Serial.println("----------------");}

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define RANGE(x, l, h) (max(min(x, h), l))

#define DEBUG 0

#define X_AXIS A0
#define Y_AXIS A1
#define SWITCH 2
const int DIP_PIN[] = {6, 5, 4, 3};
#define DIRECTION 7
#define ENABLE 12

#define ENC_A 9
#define ENC_B 13
#define ENC_SW 10

#define MIN_NOTE 36
#define MAX_NOTE 102
#define MID_NOTE ((MIN_NOTE + MAX_NOTE) >> 1)

#define MELODY_VELOCITY 100
#define CHORD_VELOCITY 70


#define FASTSTEP 40
#define SLOWSTEP 120
#define FASTDECAY 3
#define SLOWDECAY 10
#define LONGPRESS 300

typedef enum { CHROMATIC, DIATONIC_MAJOR, DIATONIC_MINOR, PENTATONIC_MAJOR, PENTATONIC_MINOR, BLUES } tuning;

tuning scale = CHROMATIC;
bool active = false;
bool contrary = false;
bool slowplay = true;
bool slowdecay = true;
bool buttonheld = false;
int transpose = 0;
int melody = MID_NOTE;
int chord[] = {MID_NOTE - 7, MID_NOTE, MID_NOTE + 9};  // define by the central note, then down a 5th and up a 6th
bool voice[] = {true, true, true, true};

unsigned resettoshowscale = 0;

#include <Adafruit_NeoPixel.h>

#define NUMPIXELS 8
#define SCALEPIXELS 6
#define PIXELPIN 11

Adafruit_NeoPixel pixels(NUMPIXELS, PIXELPIN, NEO_GRB + NEO_KHZ800);

Bounce joystick = Bounce(SWITCH, 50);
Bounce enable = Bounce(ENABLE, 50);
Bounce direction = Bounce(DIRECTION, 50);
Bounce dipsw[] = { Bounce(DIP_PIN[0], 50), Bounce(DIP_PIN[1], 50), Bounce(DIP_PIN[2], 50), Bounce(DIP_PIN[3], 50) };
Encoder encoder = Encoder(ENC_A, ENC_B);
Bounce encoder_sw = Bounce(ENC_SW, 50);

void noteOn(byte pitch, byte velocity)
{
  if (!active)
    return;
  Serial.write(0x90);
  Serial.write(pitch);
  Serial.write(velocity);
}

void noteOff(int pitch) 
{
  Serial.write(0x90);
  Serial.write(pitch);
  Serial.write(0x00);
}

void sendAllNotesOff()
{
  Serial.write(0xB0);
  Serial.write(0x7B);
  Serial.write(0x00);
  // VSTs ignore the message :-(
  for (int i = MIN_NOTE; i <= MAX_NOTE; ++i)
    noteOff(i);
}

int quantise(int n, tuning t, bool applytranspose)
{
  // C C# D D# E F F# G G# A A# B 
  // 0 1  2 3  4 5 6  7 8  9 10 11
  // diatonic:     0 2 4 5 7 9 11
  // diatonic_m:   0 2 3 5 7 8 11
  // pentatonic:   0 2 5 7 9
  // pentatonic_m: 0 3 5 7 10
  // blues:        0 3 5 6 7 10

  if (applytranspose)
    n += transpose;
  if (t == CHROMATIC)
    return n;

  int k = (144 + n + 8) % 12;   
  if (t == DIATONIC_MAJOR)
  {
    // if sharp/flat then drop a semitone
    if (k == 1 || k == 3 || k == 6 || k == 8 || k == 10)
      n--;
  }
  else if (t == DIATONIC_MINOR)
  {
    // if sharp/flat then drop a semitone
    if (k == 1 || k == 4 || k == 6 || k == 9)
      n--;
    else if (k == 10)
      n -= 2;
  }
  else if (t == PENTATONIC_MAJOR)
  {
    // 0 2 5 7 9
    switch (k)
    {
      case 1: case 3: case 6: case 8: case 10:
        n--;
        break;
      case 4: case 11:
        n -= 2;
        break;
    }
  }
  else if (t == PENTATONIC_MINOR)
  {
    // 0 3 5 7 10
    switch (k)
    {
      case 1: case 4: case 6: case 8: case 11:
        n--;
        break;
      case 2: case 9:
        n -= 2;
        break;
    }
  }
  else if (t == BLUES)
  {
    // 0 3 5 6 7 10
    switch (k)
    {
      case 1: case 4: case 8: case 11:
        n--;
        break;
      case 2: case 9:
        n -= 2;
        break;
    }
  }

  return n;
}

int smoothpot(int newvalue, int curvalue, int n)
{
  return floor(((n - 1) * curvalue + newvalue) / n);
}

void showTranspose(int d)
{
  bool down = d < 0;
  d = quantise(abs(d), scale, false);
  for (int i = 0; i < SCALEPIXELS; ++i)
  {
    int j = 2 * i + 1;
    int k = 2 * (SCALEPIXELS - i) - 1;
    bool on = down ? d >= k : d >= j;
    bool half = down ? d == k : d == j;
    if (on)
    {
      if (down) // orange and yellow
        pixels.setPixelColor(i, pixels.Color(half ? 75 : 100, half ? 50 : 25, 0));
      else  // magenta and cyan
        pixels.setPixelColor(i, pixels.Color(half ? 0 : 50, half ? 50 : 0, 50));
    }
    else
      pixels.setPixelColor(i, pixels.Color(0, 0, 0));
  }
  pixels.show();
}

void showScale()
{
  for (int i = 0; i < SCALEPIXELS; ++i)
  {
    if (contrary)
      pixels.setPixelColor(i, pixels.Color(i <= scale ? 0 : 50, 0, i <= scale ? 50 : 0));
    else
      pixels.setPixelColor(i, pixels.Color(i <= scale ? 0 : 50, i <= scale ? 50 : 0, 0));
  }
  pixels.show();
}

void checkEncoder()
{
  static long encpos = 0;
  long newpos = encoder.read();
  newpos = MIN(48, MAX(-48, newpos));
  if (newpos != encpos && newpos % 4 == 0) 
  {
    encpos = newpos;
    showTranspose(encpos / 4);
    resettoshowscale = millis() + 1000;
#if DEBUG
    DBGn(encpos)MSGn(" -> ")MSG(quantise(encpos / 4, scale, false))
#endif    
  }
  encoder.write(newpos);

  static bool enclongpress = false;
  encoder_sw.update();
  if (!enclongpress && encoder_sw.read() == 0)
  {
    if (encoder_sw.duration() > LONGPRESS)
    {
      transpose = 0;
      encpos = 0;
      encoder.write(0);
      enclongpress = true;
#if DEBUG
    MSG("Cancelling transposition");
#endif          
    }
  }
  else if (encoder_sw.rose())
  {
    if (!enclongpress)
    {
      transpose = quantise(encpos / 4, scale, false);
#if DEBUG
      DBG(transpose);
#endif    
    }
    enclongpress = false;
    showTranspose(transpose);
    resettoshowscale = millis() + 1000;
  }
}

void checkDIPswitches()
{
  // switch 1 toggles the middle chord note
  if (dipsw[0].fallingEdge())
    voice[2] = true;
  if (dipsw[0].risingEdge())
    voice[2] = false; 
    
  // switch 2 toggles the out chord notes
  if (dipsw[1].fallingEdge())
  {
    voice[1] = true;
    voice[3] = true;
  }
  if (dipsw[1].risingEdge())
  {
    voice[1] = false;
    voice[3] = false;
  }

  // switch 3 toggles fast/slow play
  if (dipsw[2].fallingEdge())
    slowplay = true;
  if (dipsw[2].risingEdge())
    slowplay = false; 

  // switch 4 toggles fast/slow play
  if (dipsw[3].fallingEdge())
    slowdecay = true;
  if (dipsw[3].risingEdge())
    slowdecay = false; 
}

void checkSwitch()
{
  int i;
  
  joystick.update();
  enable.update();
  direction.update();
  for (i = 0; i < 4; ++i)
    dipsw[i].update();

  if (joystick.risingEdge())
  {
    buttonheld = false;
    if (joystick.previousDuration() < LONGPRESS)
    {
      scale = scale + 1;
      if (scale > BLUES)
        scale = CHROMATIC;
      showScale();
#if DEBUG
      MSGn("press: ")DBG(scale)
#endif 
    }
  }
  else if (!buttonheld && joystick.read() == 0)
  {
    if (joystick.duration() > LONGPRESS)
    {
      buttonheld = true;
#if DEBUG
      MSG("long joystick press")
#endif 
    }
  }
  if (enable.fallingEdge())
  {
    active = !active;
#if DEBUG
    MSGn("Enable: ")DBG(active)
#endif
    if (active)
    {
      pixels.setPixelColor(6, pixels.Color(50, 50, 50));
      pixels.setPixelColor(7, pixels.Color(50, 50, 50));
      pixels.show();
#if !DEBUG      
      sendAllNotesOff();  // start clean
      if (voice[0]) noteOn(melody, MELODY_VELOCITY);
      if (voice[1]) noteOn(chord[0], CHORD_VELOCITY);
      if (voice[2]) noteOn(chord[1], CHORD_VELOCITY);
      if (voice[3]) noteOn(chord[2], CHORD_VELOCITY);
#endif      
    } else {
#if !DEBUG      
//      noteOff(melody);
//      noteOff(chord[0]);
//      noteOff(chord[1]);
//      noteOff(chord[2]);
      sendAllNotesOff();  // ensure all off
#endif      
      pixels.setPixelColor(6, pixels.Color(0, 0, 0));
      pixels.setPixelColor(7, pixels.Color(0, 0, 0));
      pixels.show();
    }
  }
  if (direction.fallingEdge())
  {
    contrary = !contrary;
    showScale();
  }

  checkDIPswitches();
}

void checkJoystick()
{
  if (buttonheld)
  {
#if DEBUG  
    MSG("Button held so no new notes")
#endif
    return; // don't play new notes
  }
  static int lastx = 512;
  static int lasty = 512;
  int x = analogRead(X_AXIS);
  int y = analogRead(Y_AXIS);
  int decay = slowdecay ? SLOWDECAY : FASTDECAY;
  lastx = smoothpot(x, lastx, decay);
  lasty = smoothpot(y, lasty, decay);
  int n1 = quantise(map(lastx, 0, 1023, MIN_NOTE, MAX_NOTE), scale, true);
  int n2 = quantise(map(lasty, 0, 1023, MIN_NOTE, MAX_NOTE), scale, true);
  if (n1 != melody)
  {
#if DEBUG  
    if (voice[0]) { MSGn("Melody: ")MSG(n1) }
#else  
    noteOff(melody);
    if (voice[0]) noteOn(n1, MELODY_VELOCITY);
#endif    
    melody = n1;
    if (active)
    {
      pixels.setPixelColor(6, pixels.Color(random(5) * 10, random(5) * 10, random(5) * 10));
      pixels.show();
    }
  }
  if (n2 != chord[1])
  {
#if DEBUG  
  MSGn("Chord off: ")MSGn(chord[0])MSGn(" ")MSGn(chord[1])MSGn(" ")MSG(chord[2])
#else
    noteOff(chord[0]);
    noteOff(chord[1]);
    noteOff(chord[2]);
#endif    
    int d = n2 - chord[1];
    chord[0] = contrary ? chord[0] - d : chord[0] + d;
    chord[1] += d;
    chord[2] += d;
#if DEBUG  
  MSGn("Chord on:  ")
  if (voice[1]) { MSGn(chord[0])MSGn(" ") }
  if (voice[2]) { MSGn(chord[1])MSGn(" ") }
  if (voice[3]) { MSGn(chord[2]) }
  MSG("")
#else  
    if (voice[1]) noteOn(chord[0], CHORD_VELOCITY);
    if (voice[2]) noteOn(chord[1], CHORD_VELOCITY);
    if (voice[3]) noteOn(chord[2], CHORD_VELOCITY);
#endif 
    if (active)
    {
      pixels.setPixelColor(7, pixels.Color(random(5) * 10, random(5) * 10, random(5) * 10));
      pixels.show();
    }   
  }
}

void setup() 
{
  delay(200);
#if DEBUG
  Serial.begin(115200);
#else  
  //  Set MIDI baud rate:
  Serial.begin(31250);
#endif
  delay(200);

#if !DEBUG
  sendAllNotesOff();
#endif

  pinMode(X_AXIS, INPUT);
  pinMode(Y_AXIS, INPUT);
  pinMode(SWITCH, INPUT_PULLUP);
  pinMode(ENABLE, INPUT_PULLUP);
  pinMode(DIRECTION, INPUT_PULLUP);
  for (int i = 0; i < 4; ++i)
  {
    pinMode(DIP_PIN[i], INPUT_PULLUP);
    voice[0] = true;
    voice[2] = digitalRead(DIP_PIN[0]) == 0;
    voice[1] = voice[3] = digitalRead(DIP_PIN[1]) == 0;
    slowplay = digitalRead(DIP_PIN[2]) == 0;
    slowdecay = digitalRead(DIP_PIN[3]) == 0;
  }
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

#if DEBUG
  for (int i = 0; i < 4; ++i)
  {
    MSGn(i + 1)MSGn(": ")DBGn(voice[i])
  }
  MSG("")
#endif 
  
  pixels.begin();
  pixels.clear();
  pixels.setBrightness(20);
  showScale();
}

void loop() 
{
  static unsigned long start = millis();
  
  checkSwitch();
  checkEncoder();
  unsigned now = millis();
  if ((start - now) % (slowplay ? SLOWSTEP : FASTSTEP) == 0) 
    checkJoystick();
  if (resettoshowscale > 0 && resettoshowscale <= now)
  {
    resettoshowscale = 0;
    showScale();
  }
}
