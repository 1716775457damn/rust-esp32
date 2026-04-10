// This file exists to make the tarot_core component a real library instead of an INTERFACE library.
// It helps with linker ordering and resolving dependencies between Rust and C.
#include <stdio.h>

void tarot_core_dummy(void) {
    // No-op
}
