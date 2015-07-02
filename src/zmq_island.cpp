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
	base_island(a,p,n,s_policy,r_policy), m_brokerHost(""), m_brokerPort(-1), m_token(""), m_receiveSocket(m_zmqContext, ZMQ_REQ)
{}

/// Constructor from population.
/**
 * @see pagmo::base_island constructors.
 */
zmq_island::zmq_island(const algorithm::base &a, const population &pop,
	const migration::base_s_policy &s_policy, const migration::base_r_policy &r_policy):
	base_island(a,pop,s_policy,r_policy), m_brokerHost(""), m_brokerPort(-1), m_token(""), m_receiveSocket(m_zmqContext, ZMQ_REQ)
{}

/// Copy constructor.
/**
 * @see pagmo::base_island constructors.
 */
zmq_island::zmq_island(const zmq_island &isl):base_island(isl), m_receiveSocket(m_zmqContext, ZMQ_REQ) // TODO: does this make sense?
{}

/// Assignment operator.
zmq_island &zmq_island::operator=(const zmq_island &isl)
{
	base_island::operator=(isl);
	return *this;
}

base_island_ptr zmq_island::clone() const
{
	return base_island_ptr(new zmq_island(*this));
}

// This method performs the local evolution for this island's population. 
void zmq_island::perform_evolution(const algorithm::base &algo, population &pop) const
{
	// TODO: Magic 
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

// The following methods should go in the constructor, but 
// I'm separating them out for easier debugging.
// They will eventually be merged into the constructor.

std::string zmq_island::get_ip(std::string broker) { 
	// TODO(important): Command injection vulnerability.
	// Look into mitigation strategies: sanitising input, etc.
	std::string command = "ip route get " + broker;
	FILE* result_stream = popen(command.c_str(), "r");
	char result_buffer[100];

	fgets(result_buffer, 100, result_stream);
	fclose(result_stream);

	std::string output(result_buffer);
	boost::algorithm::trim_right(output);

	auto pos = output.find("src");
	if(pos == -1) {
		return "127.0.0.1"; // TODO: What to do if there's no route to the broker?
	}

	pos += 4; // "src "
	return output.substr(pos, -1);
}

void zmq_island::set_broker_details(std::string host, int port) {
	m_brokerHost = host;
	m_brokerPort = port;
}

void zmq_island::set_token(std::string token) {
	m_token = token;
}

void zmq_island::connect(std::string host) {
	zmq::socket_t* sck = new zmq::socket_t(m_zmqContext, ZMQ_REQ);
	std::cout << "DEBUG: Opening connection to " << host << std::endl;

	sck->connect(("tcp://" + host).c_str());
	m_remoteConnections.push_back(sck);
}

bool zmq_island::initialise() {
	if(m_brokerHost == "" || m_brokerPort == -1 || m_token == "") {
		return false; // Can't initialise if we're missing those parameters
	}

	// Connect to the broker
	if(!m_brokerConn.connect(m_brokerHost, m_brokerPort)) {
		std::cout << "ERROR: Can't connect to broker" << std::endl; // TODO: better error reporting
		return false; // can't connect to broker
	}

	// Obtain IP address to advertise to the peers
	m_IP = get_ip(m_brokerHost);

	// Choose a port between 1000 and 2000
	m_localPort = rand() % 2000 + 1000;
	m_IP += ":" + std::to_string(m_localPort);

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
		connect(*it);
	}

	// Add ourselves to the list of islands on the chosen topic.
	m_brokerConn.commandSync<int>({"SADD", brokerKey, m_IP});

	// Broadcast that we've added ourselves to the list
	m_brokerConn.commandSync<int>({"PUBLISH", brokerKey + ".control", "connected:" + m_IP});

	// Open incoming socket
	m_receiveSocket.bind(("tcp://" + m_IP).c_str());
	// TODO: What to do if port is already in use?

	return true;
}

// TODO: This should go in the destructor (obviously)
void zmq_island::close() { 
	std::string brokerKey = "pagmo.islands." + m_token;
	
	m_brokerConn.commandSync<int>({"SREM", brokerKey, m_IP});
	m_brokerConn.commandSync<int>({"PUBLISH", brokerKey + ".control", "disconnected:" + m_IP});

	m_brokerConn.disconnect();

	std::cout << "DEBUG: Closed" << std::endl;
}

}

BOOST_CLASS_EXPORT_IMPLEMENT(pagmo::zmq_island)
