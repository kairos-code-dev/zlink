import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { SharedSettings } from './configuration';

function createZoneWorldLocationStore(shared: SharedSettings): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${shared.redisEndpoint}`,
    keyPrefix: `${shared.redisKeyPrefix}location`
  });
}

function zoneWorldLocationOptions(): {
  pollingIntervalMs: number;
  heartbeatIntervalMs: number;
  ownerLeaseTtlMs: number;
  routingIdFencingMarginMs: number;
  ownerLeaseRenewTimeoutMs: number;
} {
  return {
    pollingIntervalMs: 100,
    heartbeatIntervalMs: 1_000,
    ownerLeaseTtlMs: 3_000,
    routingIdFencingMarginMs: 500,
    ownerLeaseRenewTimeoutMs: 500
  };
}

export { createZoneWorldLocationStore, zoneWorldLocationOptions };
