#include <interrupts.h>
#include <esp8266_undocumented.h>
#include <umm_malloc/umm_malloc.h>

#ifndef DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION
#define DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION(...) __asm__ __volatile__("" ::: "a0", "memory")
#endif

extern "C" _xtos_handler _xtos_exc_handler_table[];

void crashMeIfYouCan(void)__attribute__((weak));
int __attribute__((noinline)) divideA_B(int a, int b);
int divideA_B(int a, int b);
int divideA_B_bp(int a, int b);

typedef void (*void_fn_t)(void);
int* nullPointer = NULL;
void_fn_t crash20_cb = (void_fn_t)0;


void printCauseTable(void) {
    uint32_t *pWord = (uint32_t *)0x3FFFC000u;
    for (int i=0; i<64; i++) {
      if ( 0 == i%4) {
        Serial.printf("\r\n0x%08x %3d ", (uint32_t)&pWord[i], i);
      }
      Serial.printf(" %08x", pWord[i]);
    }
    Serial.println("\r\n");

    Serial.println("C Wrapper called functions");
    pWord = (uint32_t *)0x3FFFC100u;
    for (int i=0; i<64; i++) {
      if ( 0 == i%4) {
        Serial.printf("\r\n0x%08x %3d ", (uint32_t)&pWord[i], i);
      }
      Serial.printf(" %08x", pWord[i]);
    }
    Serial.println("\r\n");

    Serial.println("_xtos_l1int_handler called functions");
    pWord = (uint32_t *)0x3FFFC200u;
    for (int i=0; i<64; i++) {
      if ( 0 == i%4) {
        Serial.printf("\r\n0x%08x %3d ", (uint32_t)&pWord[i], i);
      }
      Serial.printf(" %08x", pWord[i]);
    }
    Serial.println("\r\n");
}

void processKey(Print& out, int hotKey) {
  switch (hotKey) {
    case 'v':
      out.println(F("Print Exception Table Vectors"));
      printCauseTable();
      out.printf(PSTR("\nputc1: %08x\n"), *(uint32_t*)0x3fffdd48u);
      out.printf(PSTR("\nputc2: %08x\n"), *(uint32_t*)0x3fffdd4cu);
      break;
    // case 'u':
    //   out.println(F("Patch Exception 20 handler using Exception 0 handler"));
    //   _xtos_set_exception_handler(/* EXCCAUSE_INSTR_PROHIBITED */ 20u, _xtos_c_handler_table[0]);
    //   break;
    case 'r':
      out.printf_P(PSTR("Reset, ESP.reset(); ...\r\n"));
      ESP.reset();
      break;
    case 't':
      out.printf_P(PSTR("Restart, ESP.restart(); ...\r\n"));
      ESP.restart();
      break;
    case 's': {
        uint32_t startTime = millis();
        out.printf_P(PSTR("Now crashing with Software WDT. This will take about 3 seconds.\r\n"));
        ets_install_putc1(ets_putc);
        while (true) {
          ets_printf("%9lu\r", (millis() - startTime));
          ets_delay_us(250000);
          // stay in an loop blocking other system activity.
        }
      }
      break;
    case 'S':
      out.printf_P(PSTR("Now crashing with Software WDT. This will take about 3 seconds.\r\n"));
      ets_printf("Emulate typical SDK deliberate infinite loop.\n");
      while (true) {}
      break;
    case 'h':
      out.printf_P(PSTR("Now crashing with Hardware WDT. This will take about 6 seconds.\r\n"));
      {
        uint32_t startTime = millis();
        // Avoid all the Core functions that play nice, so we can hog
        // the system and crash.
        ets_install_putc1(ets_putc);
        xt_rsil(15); // Block Soft WDT
        while (true) {
          ets_printf("%9lu\r", (millis() - startTime));
          ets_delay_us(250000);
          // stay in an loop blocking other system activity.
          //
          // Note:
          // Hardware WDT kicks in if Software WDT is unable to perform.
          // With the Hardware WDT, nothing is saved on the stack.
        }
      }
      break;
    case 'H':
      out.printf_P(PSTR("Now crashing with Hardware WDT. This will take about 6 seconds.\r\n"));
      {
        xt_rsil(15); // Block Soft WDT
        ets_printf("Emulate typical SDK deliberate infinite loop w/Interrupts off.\r\n");
        while (true) {}
      }
      break;
    case 'p':
      out.println(F("Time to panic()!"));
      panic();
      break;
    case 'z':
      out.println(F("Crashing by dividing by zero. This should generate an exception(0) converted to exception(6) by Postmortem."));
      out.println();
      out.printf_P(PSTR("This should not print %d\n"), divideA_B(1, 0));
      break;
    case 'w':
      out.println(F("Now calling: 'void (*crash20_cb)(void) = NULL; void crash20_cb(void);"));
      out.println(F("This function has a prototype but was missing when the sketch was linked."));
      out.println(F("This will cause an exception 20 by calling a null function pointer ..."));
      out.println();
      crash20_cb(); // call null callback pointer
      break;
    case 'W':
      out.println(F("Now calling: void crashMeIfYouCan(void)__attribute__((weak));"));
      out.println(F("This function has a prototype but was missing when the sketch was linked."));
      out.println(F("This will cause an exception 20 by calling a null function pointer ..."));
      out.println();
      crashMeIfYouCan(); // missing weak function
      break;
    case '5': {
        uint32_t d;
        out.println(F("Unaligned Load."));
        out.println(F("This will cause an exception 9 ..."));
        out.println();
        // uint32_t save_ps = xt_rsil(2);
        out.printf_P(PSTR("0x%08X "), ((uint32 *)0x40100000)[1]);
        out.printf_P(PSTR("0x%08X "), ((uint32 *)0x40100000)[0]);
        asm volatile("movi %0, %1\n\t"
                     "l32i %0, %0, 0\n\t" :"=&r"(d) :"n"(0x40100002):);
        out.printf_P(PSTR("0x%08X\r\n"), d);
        // out.printf_P(PSTR("0x%08X\r\n"), ((uint32 *)0x40100002)[0]);
        // xt_wsr_ps(save_ps);
        out.println(F("Where's the kaboom?! There's supposed to be an Earth-shattering kaboom!"));
      }
      break;
    case '6': {
        out.println(F("Unaligned Load."));
        out.println(F("This will cause an exception 9 ..."));
        out.println();
        // out.printf_P(PSTR("0x%08X 0x%08X 0x%08X\r\n"), ((uint32 *)0x3FFFFF00)[0],  ((uint32 *)0x3FFFFF01)[0], ((uint32 *)0x3FFFFF03)[0]);
        // out.println(F("Where's the kaboom?! There's supposed to be an Earth-shattering kaboom!"));
        // I think the compiler has improved again and is correcting missaligned pointers or optomized our bug.
        uint32_t tmp;
        __asm__ __volatile__ ("movi %0, 0x3FFFFF01\n\tl32i %0, %0, 0\n\t" : "=r"(tmp) :: "memory");
        out.printf_P(PSTR("0x%08X\r\n"), tmp);
        // xt_wsr_ps(save_ps);
      }
      break;
    case '7': {
        out.println(F("Load data using a null pointer while at INTLEVEL 2."));
        out.println(F("This will cause an exception 28 ..."));
        out.println();
        uint32_t save_ps = xt_rsil(2);
        out.printf_P(PSTR("%d"), *nullPointer);
        xt_wsr_ps(save_ps);
        out.println(F("Where's the kaboom?! There's supposed to be an Earth-shattering kaboom!"));
      }
      break;
    case '8': {
        out.println(F("Load data using a null pointer."));
        out.println(F("This will cause an exception 28 ..."));
        out.println();
        // uint32_t save_ps = xt_rsil(2);
        out.printf_P(PSTR("%d"), *nullPointer);
        // xt_wsr_ps(save_ps);
        out.println(F("Where's the kaboom?! There's supposed to be an Earth-shattering kaboom!"));
      }
      break;
    case '9': {
        out.println(F("Store data using a null pointer."));
        out.println(F("This will cause an exception 29 ..."));
        out.println();
        // uint32_t save_ps = xt_rsil(2);
        *nullPointer = 42;
        // xt_wsr_ps(save_ps);
        out.println(F("Where's the kaboom?! There's supposed to be an Earth-shattering kaboom!"));
      }
      break;
    case 'b':
      out.println(F("Executing a hard coded 'break 1, 15;' w/o GDB will cause a HWDT reset."));
      out.println();
      asm volatile("break 1, 15;");
      out.println(F("This line will not be printable w/o running GDB"));
      break;
    case 'B': {
        out.println(F("Executing a hard coded 'break 1, 15;' at INTLEVEL 2 w/o GDB."));
        out.println();
        uint32_t save_ps = xt_rsil(2);
        asm volatile("break 1, 15;");
        xt_wsr_ps(save_ps);
        out.println(F("This line prints, because at INTLEVEL 2 and above, breakpoints are ignored."));
      }
      break;
    case 'i':
      out.println(F("Execute an illegal instruction."));
      __asm__ __volatile__("ill\n\t"::: "memory");
      break;
    case 'o': {
        out.println(F("Bump Heap OOM counter"));
        void* pc = malloc(128*1024u);
        if (pc) {
          free(pc);
          out.println(F("Heap OOM counter bumped"));
        }
        out.printf(PSTR("Heap OOM count: %u\r\n"), umm_get_oom_count());
      }
      break;
    case '0':
      out.println(F("Crashing at an embedded 'break 1, 15' instruction that was generated"));
      out.println(F("by the compiler after detecting a divide by zero."));
      out.println();
      out.printf_P(PSTR("This should not print %d\n"), divideA_B_bp(1, 0));
      break;
    case '1': {
        out.println(F("Ignore embedded 'break 1, 15' instruction that was generated"));
        out.println(F("by the compiler after detecting a divide by zero."));
        out.println();
        uint32_t save_ps = xt_rsil(2);
        /*
          The compiler detected a divide by zero at build time. It embeded a break 1,15 and didn't finish
          compiling the remainder of this case. With INTLEVEL at 2, xtensa will
          ignore the break 1,15 and execution falls through to the next case. :(
          Or worse, unexpectedly crashes with unusual exceptions.
        */
        {out.printf_P(PSTR("This should not print %d\n"), divideA_B_bp(1, 0));}
        xt_wsr_ps(save_ps);
        out.printf_P(PSTR("This should print if the compiler finshed compiling this scope. It does not!\n"));
      }
      break;
    case 'a':
        out.printf_P(PSTR("This does print! And, should not!\n"));
        break;
    case '\r':
      out.println();
    case '\n':
      break;
    case '?':
      out.println();
      out.println(F("Press a key + <enter>"));
      out.println(F("  v    - Print Exception Table Vectors"));
      out.println(F("  o    - Bump Heap OOM counter"));
      // out.println(F("  u    - Install Exception 20 patch"));
      out.println(F("  r    - Reset, ESP.reset();"));
      out.println(F("  t    - Restart, ESP.restart();"));
      out.println(F("  ?    - Print Help"));
      out.println();
      out.println(F("Crash with:"));
      out.println(F("  s    - Software WDT"));
      out.println(F("  S    - Software WDT - Deliberate infinite loop format found in the SDK"));
      out.println(F("  h    - Hardware WDT - looping with interrupts disabled"));
      out.println(F("  H    - Hardware WDT - Deliberate infinite loop format found in the SDK"));
      out.println(F("  w    - Hardware WDT - Exception 20, calling a NULL calback pointer."));
      out.println(F("  W    - Hardware WDT - Exception 20, calling a missing (weak) function (null pointer function)."));
      out.println(F("  0    - Hardware WDT - a hard coded compiler breakpoint from a compile time detected divide by zero"));
      // out.println(F("  1    - Hardware WDT - a hard coded compiler breakpoint from a compile time detected divide by zero with INTLEVEL 2"));
      out.println(F("  1    - Ignored BP   - a hard coded compiler breakpoint from a compile time detected divide by zero with INTLEVEL 2"));
      out.println(F("  6    - Exception 9  - Unaligned Load or Store operation"));
      out.println(F("  7    - Exception 28 - Load data using a null pointer while at INTLEVEL 2."));
      out.println(F("  8    - Exception 28 - Load data using a null pointer."));
      out.println(F("  9    - Exception 29 - Store data using a null pointer."));
      out.println(F("  b    - Hardware WDT - a forgotten hard coded 'break 1, 15;' and no GDB running."));
      out.println(F("  B    - a forgotten hard coded 'break 1, 15;' at INTLEVEL 2 and no GDB running."));

      out.println(F("  i    - Illegal instruction exception"));
      out.println(F("  z    - Divide by zero, fails w/exception(0) in __divsi3"));
      out.println(F("  p    - panic();"));
      out.println();
      break;
    default:
      out.printf_P(PSTR("\"%c\" - Not an option?  / ? - help"), hotKey);
      out.println();
      break;
  }
}

// With the current toolchain 10.1, using this to divide by zero will *not* be
// caught at compile time.
int __attribute__((noinline)) divideA_B(int a, int b) {
  DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION();
  return (a / b);
}

// With the current toolchain 10.1, using this to divide by zero *will* be
// caught at compile time. And a hard coded breakpoint will be inserted.
int divideA_B_bp(int a, int b) {
  DEBUG_ESP_BACKTRACELOG_LEAF_FUNCTION();
  return (a / b);
}
