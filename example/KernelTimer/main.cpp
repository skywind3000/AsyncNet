#include "../../system/itimer.c"
#include "CoreTimer.cpp"

Scheduler sched;

class Node
{
public:
	Node(): timer_ping(&sched) {

		// std::function 调用成员函数
		timer_ping.callback = [this](Timer *timer) {
			on_timer(timer);
		};

		// std::function 直接指向成员函数
		timer_ping.callback = std::bind(Node::on_timer, this, std::placeholders::_1);
	}

	void on_timer(Timer *timer) {
	}

private:
	Timer timer_ping;
};

