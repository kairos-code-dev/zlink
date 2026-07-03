import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

export interface RedisLocationOptions {
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
}

export function createRedisLocationStore(options: RedisLocationOptions): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${options.redisEndpoint}`,
    keyPrefix: options.redisKeyPrefix
  });
}

export function locationMessagingOptions(): { pollingIntervalMs: number; heartbeatIntervalMs: number; ownerLeaseTtlMs: number } {
  return {
    pollingIntervalMs: 100,
    heartbeatIntervalMs: 1000,
    ownerLeaseTtlMs: 5000
  };
}
