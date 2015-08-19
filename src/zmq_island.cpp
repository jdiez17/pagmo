/*****************************************************************************
 *   Copyright (C) 2004-2015 The PaGMO development team,                     *
 *   Advanced Concepts Team (ACT), European Space Agency (ESA)               *
 *                                                                           *
 *   https://github.com/esa/pagmo                                            *
 *                                                                           *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/algorithm/string.hpp>
#include <redox.hpp>
#include <list>
#include <set>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <time.h>

#include "algorithm/base.h"
#include "base_island.h"
#include "exceptions.h"
#include "zmq_island.h"
#include "migration/base_r_policy.h"
#include "migration/base_s_policy.h"
#include "population.h"
#include "problem/base.h"

namespace pagmo
{

/// Constructor from problem::base, algorithm::base, number of individuals, migration probability and selection/replacement policies.
/**
 * @see pagmo::base_island constructors.
 */
zmq_island::zmq_island(const algorithm::base &a, const problem::base &p, int n,
	const migration::base_s_policy &s_policy, const migration::base_r_policy &r_policy):
	base_island(a,p,n,s_policy,r_policy), m_brokerHost(""), m_brokerPort(-1), m_token(""),
	m_initialised(false), m_evolve(true), m_callback(NULL), 
	m_zmqContext(1), m_publisherSocket(m_zmqContext, ZMQ_PUB), m_subscriptionSocket(m_zmqContext, ZMQ_SUB)
{}

/// Constructor from population.
/**
 * @see pagmo::base_island constructors.
 */
zmq_island::zmq_island(const algorithm::base &a, const population &pop,
	const migration::base_s_policy &s_policy, const migration::base_r_policy &r_policy):
	base_island(a,pop,s_policy,r_policy), m_brokerHost(""), m_brokerPort(-1), m_token(""),
	m_initialised(false), m_evolve(true), m_callback(NULL),
	m_zmqContext(1), m_publisherSocket(m_zmqContext, ZMQ_PUB), m_subscriptionSocket(m_zmqContext, ZMQ_SUB)
{}

/// Copy constructor.
/**
 * @see pagmo::base_island constructors.
 */
zmq_island::zmq_island(const zmq_island &isl):base_island(isl),
	m_initialised(false), m_evolve(true), m_callback(NULL), // TODO: does this make sense?
	m_zmqContext(1), m_publisherSocket(m_zmqContext, ZMQ_PUB), m_subscriptionSocket(m_zmqContext, ZMQ_SUB)
{
	m_brokerHost = isl.m_brokerHost;
	m_brokerPort = isl.m_brokerPort;
	m_token = isl.m_token;
	m_IP = isl.m_IP;
	m_localPort = isl.m_localPort;
	m_evolve = isl.m_evolve;

	connect();
	std::cout << "yep this is copy" << std::endl;
}

/// Destructor.
zmq_island::~zmq_island() {
	disconnect();
}

/// Assignment operator.
zmq_island &zmq_island::operator=(const zmq_island &isl)
{
	base_island::operator=(isl);
	return *this;
}

base_island_ptr zmq_island::clone() const
{
	disconnect();
	return base_island_ptr(new zmq_island(*this));
}

// This method performs the local evolution for this island's population.
void zmq_island::perform_evolution(const algorithm::base &algo, population &pop) const
{
	if(m_evolve) {
		algo.evolve(pop);
	}

	if(m_initialised) {
		const boost::shared_ptr<population> pop_copy(new population(pop));
		const algorithm::base_ptr algo_copy = algo.clone();
		//const std::pair<boost::shared_ptr<population>,algorithm::base_ptr> out(pop_copy,algo_copy);
		const boost::shared_ptr<population> out = pop_copy;

		// First, we send a copy of our population and algorithm
		std::stringstream ss;
		boost::archive::text_oarchive oa(ss);
		oa << out;
		std::string buffer(ss.str());
		zmq::message_t msg(buffer.size());
		memcpy((void *) msg.data(), buffer.c_str(), buffer.size() - 1);
		m_publisherSocket.send(msg);

		// See if there is any data available
		zmq::message_t incoming;
#if ZMQ_VERSION >= ZMQ_MAKE_VERSION(3, 0, 0)
		// https://github.com/zeromq/libzmq/blob/master/NEWS#L548
		if(m_subscriptionSocket.recv(&incoming, ZMQ_DONTWAIT) > 0) {
#else
		if(m_subscriptionSocket.recv(&incoming, ZMQ_NOBLOCK) > 0) {
#endif
			if(incoming.size()) {
				m_callback(incoming);

				try {
					std::string bytes_in((char *) incoming.data(), incoming.size());

					std::stringstream incoming_ss(bytes_in);
					boost::archive::text_iarchive ia(incoming_ss);
					boost::shared_ptr<population> in;

					ia >> in;
					pop = *in;
				} catch (const boost::archive::archive_exception &e) {
					std::cout << "ZMQ Recv Error during island evolution using " << algo.get_name() << ": " << e.what() << std::endl;
				} catch (...) {
					std::cout << "ZMQ Recv Error during island evolution using " << algo.get_name() << ", unknown exception caught. :(" << std::endl;
				}
			}
		}
	}
}

/// Return a string identifying the island's type.
// TODO: Add topic string
/**
 * @return the string "ZMQ island".
 */
std::string zmq_island::get_name() const
{
	return "ZMQ island";
}

void zmq_island::set_broker_details(std::string host, int port) {
	m_brokerHost = host;
	m_brokerPort = port;
}

void zmq_island::set_token(std::string token) {
	m_token = token;
}

void zmq_island::connect_host(std::string host) {
	std::cout << "DEBUG: Opening connection to " << host << std::endl;

	m_subscriptionSocket.connect(("tcp://" + host).c_str());
}

bool zmq_island::connect() {
	if(m_brokerHost == "" || m_brokerPort == -1 || m_token == "" || m_IP == "") {
		return false; // Can't initialise if we're missing those parameters
	}

	// Connect to the broker
	if(!m_brokerConn.connect(m_brokerHost, m_brokerPort) || !m_brokerSubscriber.connect(m_brokerHost, m_brokerPort)) {
		std::cout << "ERROR: Can't connect to broker" << std::endl; // TODO: better error reporting
		return false; // can't connect to broker
	}

	// Initialise subscription socket
	m_subscriptionSocket.setsockopt(ZMQ_SUBSCRIBE, "", 0);

	std::cout << "DEBUG: IP: '" << m_IP << "'" << std::endl;

	std::string brokerKey = "pagmo.islands." + m_token;
	// Get list of peers
	redox::Command<std::unordered_set<std::string> >& result =
		m_brokerConn.commandSync<std::unordered_set<std::string> >({"SMEMBERS", brokerKey});

	if(!result.ok()) {
		std::cout << "ERROR: Unable to get list of peers" << std::endl;
		return false;
	}

	// Connect to peers
	auto peers = result.reply();
	for(auto it = peers.begin(); it != peers.end(); ++it) {
		connect_host(*it);
	}

	// Add ourselves to the list of islands on the chosen topic.
	m_brokerConn.commandSync<int>({"SADD", brokerKey, m_IP});

	// Broadcast that we've added ourselves to the list
	m_brokerConn.commandSync<int>({"PUBLISH", brokerKey + ".control", "connected/" + m_IP});

	// Open incoming socket
	m_publisherSocket.bind(("tcp://" + m_IP).c_str());

	// Connect to new peers when they advertise on the control channel
	m_brokerSubscriber.subscribe(brokerKey + ".control", [&](const std::string&, const std::string& msg) {
		std::vector<std::string> data;
		boost::split(data, msg, boost::is_any_of("/"));
		if(data[0] == "connected") {
			connect_host(data[1]);
		} else { /* disconnect */ }
	});

	m_initialised = true;

	return true;
}

void zmq_island::disconnect() {
	if(m_initialised) {
		std::string brokerKey = "pagmo.islands." + m_token;

		m_brokerConn.commandSync<int>({"SREM", brokerKey, m_IP});
		m_brokerConn.commandSync<int>({"PUBLISH", brokerKey + ".control", "disconnected/" + m_IP});

		m_brokerConn.disconnect();
		m_brokerSubscriber.disconnect();

		m_publisherSocket.disconnect(("tcp://" + m_IP).c_str());

		std::cout << "DEBUG: Closed" << std::endl;
	}
}

void zmq_island::set_evolve(bool e) {
	m_evolve = e;
}

bool zmq_island::get_evolve() {
	return m_evolve;
}

void zmq_island::set_callback(zmq_island::callback c) {
	m_callback = c;
}

void zmq_island::disable_callback() {
	m_callback = NULL;
}

void zmq_island::set_ip(std::string ip) {
	srand(time(0));
	m_localPort = rand() % 2000 + 1000;
	m_IP += ip + ":" + std::to_string(m_localPort);
}

}

BOOST_CLASS_EXPORT_IMPLEMENT(pagmo::zmq_island)
