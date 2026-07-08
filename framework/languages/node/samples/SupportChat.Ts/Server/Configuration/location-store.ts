import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { SupportChatServerConfig } from './sample-config';

function createSupportChatLocationStore(
  config: Pick<SupportChatServerConfig, 'redisEndpoint' | 'redisKeyPrefix'>
): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function supportChatLocationOptions(): {
  pollingIntervalMs: number;
  heartbeatIntervalMs: number;
  ownerLeaseTtlMs: number;
} {
  return {
    pollingIntervalMs: 100,
    heartbeatIntervalMs: 1000,
    ownerLeaseTtlMs: 5000
  };
}

export { createSupportChatLocationStore, supportChatLocationOptions };
