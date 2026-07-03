import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { DeliveryDispatchServerConfig } from './sample-config';

function createDeliveryDispatchLocationStore(
  config: Pick<DeliveryDispatchServerConfig, 'redisEndpoint' | 'redisKeyPrefix'>
): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function deliveryDispatchLocationOptions(): {
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

export { createDeliveryDispatchLocationStore, deliveryDispatchLocationOptions };
