/* In the place of RQ MOD's include/vm.h: the 3D renderer asks the virtual machine only for the game's
 * clock (the view's own here, so a shot of a frame comes out the same each time) and for VM_ERROR,
 * which is ERROR in the engine's release build. */
#ifndef SYSTEM4_VM_H
#define SYSTEM4_VM_H

#include <stdint.h>
#include "system4.h"

uint64_t vm_game_ticks(void);

#define VM_ERROR ERROR

#endif /* SYSTEM4_VM_H */
