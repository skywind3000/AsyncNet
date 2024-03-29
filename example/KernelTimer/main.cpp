#include <stdio.h>
#include <windows.h>
#include <mmsystem.h>

#include "../../system/itimer.c"
#include "CoreTimer.cpp"

Scheduler sched(1);

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
		printf("ping remain: %d  ts: %d\n", timer->remain(), timer->timestamp);
	}

private:
	Timer timer_ping;
};


int main()
{
	Node node;
	while (1) {
		Sleep(10);
		sched.update(timeGetTime());
	}
	return 0;
}


