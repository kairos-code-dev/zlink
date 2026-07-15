import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { BingoSampleConfig } from './sample-config';

function createBingoLocationStore(config: Pick<BingoSampleConfig, 'redisEndpoint' | 'redisKeyPrefix'>): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function bingoLocationOptions(): {
  pollingIntervalMs: number;
  heartbeatIntervalMs: number;
  ownerLeaseTtlMs: number;
  routingIdFencingMarginMs: number;
  ownerLeaseRenewTimeoutMs: number;
} {
  return {
    pollingIntervalMs: 100,
    heartbeatIntervalMs: 10_000,
    ownerLeaseTtlMs: 30_000,
    routingIdFencingMarginMs: 5_000,
    ownerLeaseRenewTimeoutMs: 3_000
  };
}

export { bingoLocationOptions, createBingoLocationStore };
