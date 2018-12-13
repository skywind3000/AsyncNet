#include <vector>
#include "../system/system.h"
#include "../system/inetbase.c"

int main()
{
	System::CriticalSection lock;
	std::vector<System::CriticalSection> locks;
	printf("Hello, World !\n");
	// locks.resize(1000);
	// locks.push_back(lock);
	return 0;
}




