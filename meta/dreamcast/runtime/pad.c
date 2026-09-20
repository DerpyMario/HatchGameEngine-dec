/* The controller, through KallistiOS's maple bus driver. */

#include "dreamcast.h"

uint32_t pad_read(void) {
    maple_device_t* device = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (!device)
        return 0;

    cont_state_t* state = (cont_state_t*)maple_dev_status(device);
    if (!state)
        return 0;

    return state->buttons;
}
