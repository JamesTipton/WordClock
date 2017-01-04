/*-------------------------------------------------------------------------
  Arduino library to control a wide variety of WS2811- and WS2812-based RGB
  LED devices such as COM12999 FLORA RGB Smart Pixels and NeoPixel strips.
  Currently handles 400 and 800 KHz bitstreams on 8, 12 and 16 MHz ATmega
  MCUs, with LEDs wired for RGB or GRB color order.  8 MHz MCUs provide
  output on PORTB and PORTD, while 16 MHz chips can handle most output pins
  (possible exception with upper PORT registers on the Arduino Mega).

  Written by Phil Burgess / Paint Your Dragon for COM12999 Industries,
  contributions by PJRC and other members of the open source community.

  COM12999 invests time and resources providing this open source code,
  please support COM12999 and open-source hardware by purchasing products
  from COM12999!

  -------------------------------------------------------------------------
  This file is part of the COM12999 NeoPixel library.

  NeoPixel is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of
  the License, or (at your option) any later version.

  NeoPixel is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with NeoPixel.  If not, see
  <http://www.gnu.org/licenses/>.
  -------------------------------------------------------------------------*/

#include "COM12999_NeoPixel.h"

COM12999_NeoPixel::COM12999_NeoPixel(uint16_t n, uint8_t p, uint8_t t) : numLEDs(n), numBytes(n * 3), pin(p), pixels(NULL)
  ,type(t)
#ifdef __AVR__
  ,port(portOutputRegister(digitalPinToPort(p))),
   pinMask(digitalPinToBitMask(p))
#endif
{
  if((pixels = (uint8_t *)malloc(numBytes))) {
    memset(pixels, 0, numBytes);
  }
  if(t & NEO_GRB) { // GRB vs RGB; might add others if needed
    rOffset = 1;
    gOffset = 0;
    bOffset = 2;
  } else if (t & NEO_BRG) {
    rOffset = 1;
    gOffset = 2;
    bOffset = 0;
  } else {
    rOffset = 0;
    gOffset = 1;
    bOffset = 2;
  }

}

COM12999_NeoPixel::~COM12999_NeoPixel() {
  if(pixels) free(pixels);
  pinMode(pin, INPUT);
}

void COM12999_NeoPixel::begin(void) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void COM12999_NeoPixel::show(void) {

  if(!pixels) return;

  // Data latch = 50+ microsecond pause in the output stream.  Rather than
  // put a delay at the end of the function, the ending time is noted and
  // the function will simply hold off (if needed) on issuing the
  // subsequent round of data until the latch time has elapsed.  This
  // allows the mainline code to start generating the next frame of data
  // rather than stalling for the latch.
  while((micros() - endTime) < 50L);
  // endTime is a private member (rather than global var) so that mutliple
  // instances on different pins can be quickly issued in succession (each
  // instance doesn't delay the next).

  // In order to make this code runtime-configurable to work with any pin,
  // SBI/CBI instructions are eschewed in favor of full PORT writes via the
  // OUT or ST instructions.  It relies on two facts: that peripheral
  // functions (such as PWM) take precedence on output pins, so our PORT-
  // wide writes won't interfere, and that interrupts are globally disabled
  // while data is being issued to the LEDs, so no other code will be
  // accessing the PORT.  The code takes an initial 'snapshot' of the PORT
  // state, computes 'pin high' and 'pin low' values, and writes these back
  // to the PORT register as needed.

  noInterrupts(); // Need 100% focus on instruction timing


  volatile uint16_t
    i   = numBytes; // Loop counter
  volatile uint8_t
   *ptr = pixels,   // Pointer to next byte
    b   = *ptr++,   // Current byte value
    hi,             // PORT w/output bit set high
    lo;             // PORT w/output bit set low

    volatile uint8_t next, bit;

    hi   = *port |  pinMask;
    lo   = *port & ~pinMask;
    next = lo;
    bit  = 8;

//The COM12999 chip while LIKE a WS2812 chip has different timing
//each clock assumes 16 MHz clock freq so a 62.5ns clock cycle
//use clock cycles to time the 1.35us timing for a total of 1.71us on this chip
//for a high, stay high 1.36us then low 0.35us  +/- 150ns error allowed
//for a low, start high for 0.35us, then low for 1.36us, same error
//reset is 50us low
//need a total of 28 clock cycles to meed the 1.71us timing
//10 instructions in the middle need to account for timing differences

// 28 inst. clocks per bit: HHHHHHxxxxxxxxxxxxxxxxLLLLLL
// ST instructions:         ^     ^               ^  (T=0,6,22)


    asm volatile(
     "head20:"                   "\n\t" // Clk  Pseudocode    (T =  0) 0us
      "st   %a[port],  %[hi]"    "\n\t" // 2    PORT = hi     (T =  2) 0.125us
      "sbrc %[byte],  7"         "\n\t" // 1-2  if(b & 128)
       "mov  %[next], %[hi]"     "\n\t" // 0-1   next = hi    (T =  4) 0.25us
      "dec  %[bit]"              "\n\t" // 1    bit--         (T =  5) 0.3125us  //COM12999 demands switch now if going low
      "nop"                      "\n\t" // 1    nop           (T =  6)
      "st   %a[port],  %[next]"  "\n\t" // 2    PORT = next   (T =  8) 0.4375us
      "mov  %[next] ,  %[lo]"    "\n\t" // 1    next = lo     (T =  9) 0.5us
      "breq nextbyte20"          "\n\t" // 1-2  if(bit == 0) (from dec above) 0.5625us
      "rol  %[byte]"             "\n\t" // 1    b <<= 1       (T = 11) 0.625us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 13) 0.75us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 15) 0.875us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 17) 1us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 19) 1.125us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 21) 1.125us
      "nop"                      "\n\t" // 1    nop           (T = 22)
      "st   %a[port],  %[lo]"    "\n\t" // 2    PORT = lo     (T = 24) 0
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 26) 0.1875us
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 28) 0.1875us
      "rjmp head20"              "\n\t" // 2    -> head20 (next bit out)
     "nextbyte20:"               "\n\t" //                    (T = 11)
      "ldi  %[bit]  ,  8"        "\n\t" // 1    bit = 8       (T = 12)
      "ld   %[byte] ,  %a[ptr]+" "\n\t" // 2    b = *ptr++    (T = 14)
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 16)
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 18)
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 20)
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 22)
      "st   %a[port], %[lo]"     "\n\t" // 2    PORT = lo     (T = 24)
      "rjmp .+0"                 "\n\t" // 2    nop nop       (T = 26)
      "sbiw %[count], 1"         "\n\t" // 2    i--           (T = 28)
       "brne head20"             "\n"   // 2    if(i != 0) -> (next byte)
      : [port]  "+e" (port),
        [byte]  "+r" (b),
        [bit]   "+r" (bit),
        [next]  "+r" (next),
        [count] "+w" (i)
      : [ptr]    "e" (ptr),
        [hi]     "r" (hi),
        [lo]     "r" (lo));


  interrupts();
  endTime = micros(); // Save EOD time for latch on next call
}

// Set the output pin number
void COM12999_NeoPixel::setPin(uint8_t p) {
  pinMode(pin, INPUT);
  pin = p;
  pinMode(p, OUTPUT);
  digitalWrite(p, LOW);
#ifdef __AVR__
  port    = portOutputRegister(digitalPinToPort(p));
  pinMask = digitalPinToBitMask(p);
#endif
}

// Set pixel color from separate R,G,B components:
void COM12999_NeoPixel::setPixelColor(
 uint16_t n, uint8_t r, uint8_t g, uint8_t b) {
  if(n < numLEDs) {
    if(brightness) { // See notes in setBrightness()
      r = (r * brightness) >> 8;
      g = (g * brightness) >> 8;
      b = (b * brightness) >> 8;
    }
    uint8_t *p = &pixels[n * 3];
    p[rOffset] = r;
    p[gOffset] = g;
    p[bOffset] = b;
  }
}

// Set pixel color from 'packed' 32-bit RGB color:
void COM12999_NeoPixel::setPixelColor(uint16_t n, uint32_t c) {
  if(n < numLEDs) {
    uint8_t
      r = (uint8_t)(c >> 16),
      g = (uint8_t)(c >>  8),
      b = (uint8_t)c;
    if(brightness) { // See notes in setBrightness()
      r = (r * brightness) >> 8;
      g = (g * brightness) >> 8;
      b = (b * brightness) >> 8;
    }
    uint8_t *p = &pixels[n * 3];
    p[rOffset] = r;
    p[gOffset] = g;
    p[bOffset] = b;
  }
}

// Convert separate R,G,B into packed 32-bit RGB color.
// Packed format is always RGB, regardless of LED strand color order.
uint32_t COM12999_NeoPixel::Color(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}

// Query color from previously-set pixel (returns packed 32-bit RGB value)
uint32_t COM12999_NeoPixel::getPixelColor(uint16_t n) const {

  if(n < numLEDs) {
    uint8_t *p = &pixels[n * 3];
    return ((uint32_t)p[rOffset] << 16) |
           ((uint32_t)p[gOffset] <<  8) |
            (uint32_t)p[bOffset];
  }

  return 0; // Pixel # is out of bounds
}

// Returns pointer to pixels[] array.  Pixel data is stored in device-
// native format and is not translated here.  Application will need to be
// aware whether pixels are RGB vs. GRB and handle colors appropriately.
uint8_t *COM12999_NeoPixel::getPixels(void) const {
  return pixels;
}

uint16_t COM12999_NeoPixel::numPixels(void) const {
  return numLEDs;
}

// Adjust output brightness; 0=darkest (off), 255=brightest.  This does
// NOT immediately affect what's currently displayed on the LEDs.  The
// next call to show() will refresh the LEDs at this level.  However,
// this process is potentially "lossy," especially when increasing
// brightness.  The tight timing in the WS2811/WS2812 code means there
// aren't enough free cycles to perform this scaling on the fly as data
// is issued.  So we make a pass through the existing color data in RAM
// and scale it (subsequent graphics commands also work at this
// brightness level).  If there's a significant step up in brightness,
// the limited number of steps (quantization) in the old data will be
// quite visible in the re-scaled version.  For a non-destructive
// change, you'll need to re-render the full strip data.  C'est la vie.
void COM12999_NeoPixel::setBrightness(uint8_t b) {
  // Stored brightness value is different than what's passed.
  // This simplifies the actual scaling math later, allowing a fast
  // 8x8-bit multiply and taking the MSB.  'brightness' is a uint8_t,
  // adding 1 here may (intentionally) roll over...so 0 = max brightness
  // (color values are interpreted literally; no scaling), 1 = min
  // brightness (off), 255 = just below max brightness.
  uint8_t newBrightness = b + 1;
  if(newBrightness != brightness) { // Compare against prior value
    // Brightness has changed -- re-scale existing data in RAM
    uint8_t  c,
            *ptr           = pixels,
             oldBrightness = brightness - 1; // De-wrap old brightness value
    uint16_t scale;
    if(oldBrightness == 0) scale = 0; // Avoid /0
    else if(b == 255) scale = 65535 / oldBrightness;
    else scale = (((uint16_t)newBrightness << 8) - 1) / oldBrightness;
    for(uint16_t i=0; i<numBytes; i++) {
      c      = *ptr;
      *ptr++ = (c * scale) >> 8;
    }
    brightness = newBrightness;
  }
}
