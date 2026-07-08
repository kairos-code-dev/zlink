import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import type { ShoppingMallServerConfig } from './sample-config';

function createShoppingMallLocationStore(
  config: Pick<ShoppingMallServerConfig, 'redisEndpoint' | 'redisKeyPrefix'>
): ZLinkRedisLocationStore {
  return new ZLinkRedisLocationStore({
    url: `redis://${config.redisEndpoint}`,
    keyPrefix: `${config.redisKeyPrefix}location`
  });
}

function shoppingMallLocationOptions(): {
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

export { createShoppingMallLocationStore, shoppingMallLocationOptions };
