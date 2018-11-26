#ifndef TACHYON_LIB_IPC_MACROS_H_
#define TACHYON_LIB_IPC_MACROS_H_

// Convenience macro to make production builds not complain about unused
// variables when we create a variable for the sole purpose of checking an
// assert.
#define _UNUSED(x) ((void)(x))

#endif // TACHYON_LIB_IPC_MACROS_H_
