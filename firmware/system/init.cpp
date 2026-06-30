#include "init.hpp"
#include "tasks.hpp"

extern "C" void kern_boot()
{
	kern_create_tasks();
}
