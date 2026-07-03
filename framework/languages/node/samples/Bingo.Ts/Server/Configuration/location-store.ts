import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { BingoSampleConfig } from './sample-config';

function createBingoLocationStore(config: Pick<BingoSampleConfig, 'redisEndpoint' | 'redisKeyPrefix'>): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function bingoLocationOptions(): { pollingIntervalMs: number; heartbeatIntervalMs: number; ownerLeaseTtlMs: number } {
  return {
    pollingIntervalMs: 100,
    heartbeatIntervalMs: 1000,
    ownerLeaseTtlMs: 5000
  };
}

export { bingoLocationOptions, createBingoLocationStore };
