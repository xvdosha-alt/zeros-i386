#include "../py/host_io.h"
#include "../py/runtime.h"
#include "../py/repl.h"
#include "../py/object.h"

int main(void)
{
    mp_io_init();
    mp_repl_run();
    return 0;
}
