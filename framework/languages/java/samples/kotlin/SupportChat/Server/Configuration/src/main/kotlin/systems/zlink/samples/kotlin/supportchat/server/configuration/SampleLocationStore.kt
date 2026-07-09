package systems.zlink.samples.kotlin.supportchat.server.configuration

import systems.zlink.framework.locations.redis.ZLinkRedisLocationOptions
import systems.zlink.framework.locations.redis.ZLinkRedisLocationStore

object SampleLocationStore {
    fun create(topology: SampleTopology = SampleTopology.create()): ZLinkRedisLocationStore =
        ZLinkRedisLocationStore(
            ZLinkRedisLocationOptions()
                .setConnectionString(topology.redisEndpoint)
                .setKeyPrefix(topology.redisKeyPrefix),
        )
}
