#pragma once

// Initialize SD card crash logging. Call once at startup after nxlink.
// Writes sdmc:/switch/Moonlight/crash_base.txt with ASLR base + main address.
void crash_handler_init(uintptr_t nro_base, uintptr_t main_addr);
