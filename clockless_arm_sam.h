#ifndef __INC_CLOCKLESS_ARM_SAM_H
#define __INC_CLOCKLESS_ARM_SAM_H

// Definition for a single channel clockless controller for the sam family of arm chips, like that used in the due and rfduino
// See clockless.h for detailed info on how the template parameters are used.

#if defined(__SAM3X8E__)

template <uint8_t DATA_PIN, int T1, int T2, int T3, EOrder RGB_ORDER = RGB, bool FLIP = false, int WAIT_TIME = 50>
class ClocklessController : public CLEDController {
	typedef typename FastPinBB<DATA_PIN>::port_ptr_t data_ptr_t;
	typedef typename FastPinBB<DATA_PIN>::port_t data_t;

	data_t mPinMask;
	data_ptr_t mPort;
	CMinWait<WAIT_TIME> mWait;
public:
	virtual void init() { 
		FastPinBB<DATA_PIN>::setOutput();
		mPinMask = FastPinBB<DATA_PIN>::mask();
		mPort = FastPinBB<DATA_PIN>::port();
	}

	virtual void clearLeds(int nLeds) {
		showColor(CRGB(0, 0, 0), nLeds, 0);
	}

	// set all the leds on the controller to a given color
	virtual void showColor(const struct CRGB & data, int nLeds, CRGB scale = CRGB::White) {
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);

		showRGBInternal<0, false>(nLeds, scale, (const byte*)&data);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS((long)nLeds * 8 * (T1 + T2 + T3));
		long millisTaken = (microsTaken / 1000);
		do { TimeTick_Increment(); } while(--millisTaken > 0);
		savedClock.restore();
		sei();
		mWait.mark();
	}

	virtual void show(const struct CRGB *rgbdata, int nLeds, CRGB scale = CRGB::White) { 
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);
		
		Serial.print("Scale is "); 
		Serial.print(scale.raw[0]); Serial.print(" ");
		Serial.print(scale.raw[1]); Serial.print(" ");
		Serial.print(scale.raw[2]); Serial.println(" ");
		// FastPinBB<DATA_PIN>::hi(); delay(1); FastPinBB<DATA_PIN>::lo();
		showRGBInternal<0, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS((long)nLeds * 8 * (T1 + T2 + T3));
		long millisTaken = (microsTaken / 1000);
		do { TimeTick_Increment(); } while(--millisTaken > 0);
		savedClock.restore();
		sei();
		mWait.mark();
	}

#ifdef SUPPORT_ARGB
	virtual void show(const struct CARGB *rgbdata, int nLeds, CRGB scale = CRGB::White) { 
		mWait.wait();
		cli();
		SysClockSaver savedClock(T1 + T2 + T3);

		showRGBInternal<1, true>(nLeds, scale, (const byte*)rgbdata);

		// Adjust the timer
		long microsTaken = CLKS_TO_MICROS((long)nLeds * 8 * (T1 + T2 + T3));
		long millisTaken = (microsTaken / 1000);
		do { TimeTick_Increment(); } while(--millisTaken > 0);
		savedClock.restore();
		sei();
		mWait.mark();
	}
#endif

// I hate using defines for these, should find a better representation at some point
#define _CTRL CTPTR[0]
#define _LOAD CTPTR[1]
#define _VAL CTPTR[2]

	__attribute__((always_inline)) static inline void wait_loop_start(register volatile uint32_t *CTPTR) {
		__asm__ __volatile__ (
			"L_%=: ldr.w r8, [%0]\n"
			"      tst.w r8, #65536\n"
			"		beq.n L_%=\n"
			: /* no outputs */
			: "r" (CTPTR)
			: "r8"
			);
	}

	template<int MARK> __attribute__((always_inline)) static inline void wait_loop_mark(register volatile uint32_t *CTPTR) {
		__asm__ __volatile__ (
			"L_%=: ldr.w r8, [%0, #8]\n"
			"      cmp.w r8, %1\n"
			"		bhi.n L_%=\n"
			: /* no outputs */
			: "r" (CTPTR), "I" (MARK)
			: "r8"
			);
	}

	__attribute__((always_inline)) static inline void mark_port(register data_ptr_t port, register int val) {
		__asm__ __volatile__ (
			"	str.w %0, [%1]\n"
			: /* no outputs */
			: "r" (val), "r" (port)
			);
	}
#define AT_BIT_START(X) wait_loop_start(CTPTR); X;
#define AT_MARK(X) wait_loop_mark<T1_MARK>(CTPTR); { X; }
#define AT_END(X) wait_loop_mark<T2_MARK>(CTPTR); { X; }

// #define AT_BIT_START(X) while(!(_CTRL & SysTick_CTRL_COUNTFLAG_Msk)); { X; }
// #define AT_MARK(X) while(_VAL > T1_MARK); { X; }
// #define AT_END(X) while(_VAL > T2_MARK); { X; }

//#define AT_MARK(X) delayclocks_until<T1_MARK>(_VAL); X; 
//#define AT_END(X) delayclocks_until<T2_MARK>(_VAL); X;

#define TOTAL (T1 + T2 + T3)

#define T1_MARK (TOTAL - T1)
#define T2_MARK (T1_MARK - T2)
	template<int MARK> __attribute__((always_inline)) static inline void delayclocks_until(register byte b) { 
		__asm__ __volatile__ (
			"	   sub %0, %0, %1\n"
			"L_%=: subs %0, %0, #2\n"
			"      bcs.n L_%=\n"
			: /* no outputs */
			: "r" (b), "I" (MARK)
			: /* no clobbers */
			);

	}

#define FORCE_REFERENCE(var)  asm volatile( "" : : "r" (var) )
#define DITHER 1
#define DADVANCE 3
	// This method is made static to force making register Y available to use for data on AVR - if the method is non-static, then 
	// gcc will use register Y for the this pointer.
	template<int SKIP, bool ADVANCE> static void showRGBInternal(register int nLeds, register CRGB scale, register const byte *rgbdata) {
		register data_ptr_t port asm("r7") = FastPinBB<DATA_PIN>::port(); FORCE_REFERENCE(port);
		register byte *data = (byte*)rgbdata;
		register uint8_t *end = data + (nLeds*3 + SKIP); 
		uint8_t E[3] = {0xFF,0xFF,0xFF};
		uint8_t D[3] = {0,0,0};

		static uint8_t Dstore[3] = {0,0,0};

		// compute the E values and seed D from the stored values
		for(register uint32_t i = 0; i < 3; i++) { 
			byte S = scale.raw[i];
			while(S>>=1) { E[i] >>=1; };
			D[i] = Dstore[i] & E[i];
		}

		register volatile uint32_t *CTPTR asm("r6")= &SysTick->CTRL; FORCE_REFERENCE(CTPTR);
		
		*port = 0;

		register uint32_t b;
		b = ADVANCE ? data[SKIP + B0] :rgbdata[SKIP + B0];
		// dither
		if(DITHER && b) b = qadd8(b, D[B0]);
		// now scale
		b = scale8(b, scale.raw[B0]);

		// Setup and start the clock
		_LOAD = TOTAL;
		_VAL = 0;
		_CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
		_CTRL |= SysTick_CTRL_ENABLE_Msk;

		// read to clear the loop flag
		_CTRL;

		while(data < end) { 

			// advance D constrained by E
			D[B0] += DADVANCE; D[B0] &= E[B0];
			D[B1] += DADVANCE; D[B1] &= E[B1];
			D[B2] += DADVANCE; D[B2] &= E[B2];

			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			b = ADVANCE ? data[SKIP + B1] : rgbdata[SKIP + B1];
			// dither
			if(DITHER && b) b = qadd8(b, D[B1]);
			// now scale
			b = scale8(b, scale.raw[B1]);

			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			b = ADVANCE ? data[SKIP + B2] : rgbdata[SKIP + B2];
			// dither
			if(DITHER && b) b = qadd8(b, D[B2]);
			// now scale
			b = scale8(b, scale.raw[B2]);
 
			for(register uint32_t i = 7; i > 0; i--) { 
				AT_BIT_START(*port = 1);
				if(b& 0x80) {} else { AT_MARK(*port = 0); }
				AT_END(*port = 0);
				b <<= 1;
			}

			AT_BIT_START(*port = 1);
			if(b& 0x80) {} else { AT_MARK(*port = 0); }
			AT_END(*port = 0);

			// We have some extra time between rgb pixels, prep 
			// the next byte and cycle the dither adjustments
			data += (3 + SKIP);
			b = ADVANCE ? data[SKIP + B0] : rgbdata[SKIP + B0];
			// dither
			if(DITHER && b) b = qadd8(b, D[B0]);
			// now scale
			b = scale8(b, scale.raw[B0]);
		};

		// Save the D values for cycling through next time
		Dstore[0] = D[0];
		Dstore[1] = D[1];
		Dstore[2] = D[2];
	}
};

#endif

#endif
