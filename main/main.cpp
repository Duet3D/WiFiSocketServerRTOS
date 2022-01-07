#include <stdbool.h>

extern void setup();
extern void loop();

extern "C" void app_main()
{
    setup();
    while (true) {
        loop ();
    }
}
