#include <stdio.h>
#include <windows.h>
#include <mmsystem.h>

#include "../../system/itimer.c"
#include "CoreTimer.cpp"

Scheduler sched;

class Node
{
public:
	Node(): timer_ping(&sched) {

		// 第一种写法 std::function 调用成员函数
		timer_ping.callback = [this](Timer *timer) {
			on_timer(timer);
		};

		// 第二种写法 std::function 直接指向成员函数
		timer_ping.callback = std::bind(&Node::on_timer, this, std::placeholders::_1);

		timer_ping.start(1000, 5);
	}

	void on_timer(Timer *timer) {
		printf("ping down-count: %d\n", timer->remain());
	}

private:
	Timer timer_ping;
};


int main()
{
	sched.init(timeGetTime(), 5);
	Node node;
	while (1) {
		Sleep(10);
		sched.update(timeGetTime());
	}
	return 0;
}


