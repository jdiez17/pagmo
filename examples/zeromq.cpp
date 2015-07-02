#include "../src/pagmo.h"

using namespace pagmo;

int main() {
    problem::dejong p(10);
    algorithm::monte_carlo a(100);
    zmq_island i(a, p, 1);

	i.set_broker_details("192.168.1.36", 6379);
	i.set_token("zeromq_test");
	i.initialise();

    //i.evolve(10);
	//
	
	i.close();

    return 0;
}
