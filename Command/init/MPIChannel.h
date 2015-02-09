#ifndef _MPI_CHANNEL_MANAGER_H_
#define _MPI_CHANNEL_MANAGER_H_

#include "Command/init/log.h"
#include "Options/CommonOptions.h"
#include <deque>
#include <algorithm>
#include <unordered_map>
#include <cassert>
#include <sstream>

enum XferDir { NONE, SEND, RECV };

/**
 * An MPI channel is defined by:
 *
 * 1. a transfer direction (SEND or RECV),
 * 2. a peer MPI rank (the MPI rank we are sending to or
 * recving from)
 * 3. an MPI tag (used in MPI to distinguish different
 * types of messages exchanged between the same pair of
 * MPI ranks)
 */
class MPIChannel
{
public:

	MPIChannel() : m_xferDir(NONE), m_peerRank(-1),
		m_mpiTag(-1) {}

	MPIChannel(XferDir xferDir, int peerRank,
		int mpiTag) : m_xferDir(xferDir),
		m_peerRank(peerRank), m_mpiTag(mpiTag)
	{ }

	bool operator==(const MPIChannel& channel) const
	{
		return channel.m_xferDir == m_xferDir &&
			channel.m_peerRank == m_peerRank &&
			channel.m_mpiTag == m_mpiTag;
	}

	std::string str() const
	{
		std::ostringstream s;
		s << "(";
		if (m_xferDir == SEND) {
			s << "SEND";
		} else {
			assert(m_xferDir == RECV);
			s << "RECV";
		}
		s << "," << m_peerRank << ","
			<< m_mpiTag << ")";
		assert(s);
		return s.str();
	}

	XferDir m_xferDir;
	int m_peerRank;
	int m_mpiTag;
};

namespace std {
	template <>
	struct hash<MPIChannel>
	{
		std::size_t operator()(const MPIChannel& k) const
		{
			using std::size_t;
			using std::hash;
			using std::string;
			return ((hash<int>()((int)k.m_xferDir)
				^ (hash<int>()(k.m_peerRank) << 1)) >> 1)
				^ (hash<int>()(k.m_mpiTag) << 1);
		}
	};
}

enum ChannelRequestResult { GRANTED, QUEUED };

/**
 * A singleton class that controls shared access to
 * *MPI channels* by different mpih client commands.
 *
 * The main purpose of the MPIChannelManager class is
 * to ensure that two client commands don't send or
 * recv data on the same MPI channel at the same time,
 * causing the unrelated data streams to become
 * intermingled.
 *
 * Here is a simple example:
 *
 *    #!/bin/bash
 *    set -eu
 *
 *    if [ $MPIH_RANK -eq 0 ]; then
 *        echo "message 1" | mpih send &
 *        echo "message 2" | mpih send &
 *    else
 *        mpih recv 0 | cat &
 *        mpih recv 0 | cat &
 *    fi
 *
 * In the above example, the two 'mpih send' commands
 * happen simultaneously; "message 1" and "message 2"
 * will likely become intermingled and it is
 * unpredictable what data will be received by each
 * the two 'mpih recv' commands.
 */
class MPIChannelManager
{
public:

	typedef std::deque<size_t> ConnectionQueue;
	typedef std::unordered_map<MPIChannel, ConnectionQueue>
		ChannelMap;

	static MPIChannelManager& getInstance()
	{
		/*
		 * Note: 'instance' will instantiated on the first
		 * call to getInstance(), and destroyed when
		 * the MPIChannelManager class is destroyed.
		 */
		static MPIChannelManager instance;
		return instance;
	}

	/** Request ownership of an MPI channel */
	ChannelRequestResult requestChannel(size_t connectionID,
		const MPIChannel& channel)
	{
		ChannelMap::iterator it = m_channelMap.find(channel);
		ChannelRequestResult result;
		if (it == m_channelMap.end()) {
			ConnectionQueue q;
			q.push_back(connectionID);
			ChannelMap::value_type newEntry =
				ChannelMap::value_type(channel, q);
			std::pair<ChannelMap::iterator, bool> inserted =
				m_channelMap.insert(newEntry);
			assert(inserted.second);
			result = GRANTED;
		} else {
			ConnectionQueue& q = it->second;
			if (q.empty())
				q.push_back(connectionID);
			assert(!q.empty());
			if (q.front() == connectionID) {
				result = GRANTED;
			} else {
				ConnectionQueue::iterator it =
					std::find(q.begin(), q.end(), connectionID);
				if (it == q.end())
					q.push_back(connectionID);
				result = QUEUED;
			}
		}
		if (opt::verbose >= 3) {
			log_f(connectionID, "%s MPI Channel %s",
				result == QUEUED ? "queued for" : "granted",
				channel.str().c_str());
		}
		return result;
	}

	/** Release ownership of an MPI channel */
	void releaseChannel(size_t connectionID,
		const MPIChannel& channel)
	{
		if (opt::verbose >= 3) {
			log_f(connectionID, "releasing MPI Channel %s",
				channel.str().c_str());
		}
		ChannelMap::iterator it = m_channelMap.find(channel);
		assert(it != m_channelMap.end());
		ConnectionQueue& q = it->second;
		assert(q.front() == connectionID);
		q.pop_front();
	}

private:

	/** private constructor (because this is a singleton class) */
	MPIChannelManager() {};

	/*
	 * disable copy constructor and assignment operator
	 * to prevent copies of the singleton instance
	 */
	MPIChannelManager(MPIChannelManager const&);
	void operator=(MPIChannelManager const&);

	/**
	 * map from MPI channels to connection IDs that
	 * currently own or are waiting on that channel.
	 */
	ChannelMap m_channelMap;
};

#endif
