#include "Command/init/MPIChannel.h"
#include <gtest/gtest.h>

TEST(MPIChannel, MPIChannelManager)
{
	MPIChannelManager& manager = MPIChannelManager::getInstance();
	ChannelRequestResult result;

	/* params for mock connection and channel */
	size_t connectionID1 = 1;
	size_t connectionID2 = 2;
	XferDir dir = SEND;
	int peerRank = 1;
	int mpiTag = 0;

	/* acquire an available channel */
	MPIChannel channel(dir, peerRank, mpiTag);
	result = manager.requestChannel(connectionID1, channel);
	ASSERT_EQ(GRANTED, result);

	/* channel should stay GRANTED if we request it again */
	result = manager.requestChannel(connectionID1, channel);
	ASSERT_EQ(GRANTED, result);

	/* request for busy channel should be QUEUED */
	result = manager.requestChannel(connectionID2, channel);
	ASSERT_EQ(QUEUED, result);

	/* release channel to next connection in queue */
	manager.releaseChannel(connectionID1, channel);
	result = manager.requestChannel(connectionID2, channel);
	ASSERT_EQ(GRANTED, result);
}
