#include "../src/pagmo.h"
#include <signal.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace pagmo;

std::atomic<bool> quit;

void sig(int) {
	quit.store(true);
}

int main() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &sig;
	sigfillset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL); // register signal handler

	quit.store(false);

    problem::dejong p(10);
    algorithm::monte_carlo a(100);
    zmq_island i(a, p, 1);

	i.set_broker_details("192.168.1.39", 6379);
	i.set_token("zeromq_test");
	i.initialise("192.168.1.39");

	while(!quit) { 
		i.evolve(1);
		std::cout << "Best: " << i.get_population().champion().x << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

    return 0;
}
