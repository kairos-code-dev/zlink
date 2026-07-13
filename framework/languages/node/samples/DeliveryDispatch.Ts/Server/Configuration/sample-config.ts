type DeliveryDispatchServerConfig = {
  dispatchApiHttpUrl: string;
  dispatchEndpoint: string;
  courierStreamEndpoint: string;
  courierSessionSpotEndpoint: string;
  courierActorNode1RouteEndpoint: string;
  courierActorNode2RouteEndpoint: string;
  courierActorNode1SpotEndpoint: string;
  courierActorNode2SpotEndpoint: string;
  trackingEndpoint: string;
  trackingSpotEndpoint: string;
  sessionStreamEndpoint: string;
  sessionSpotRouterEndpoint: string;
  sessionSpotNodeRid: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
};

function loadSampleConfig(): DeliveryDispatchServerConfig {
  return {
    dispatchApiHttpUrl: process.env.DELIVERYDISPATCH_API_HTTP ?? 'http://127.0.0.1:31083',
    dispatchEndpoint: process.env.DELIVERYDISPATCH_CENTER_ROUTE ?? 'tcp://127.0.0.1:31084',
    courierStreamEndpoint: process.env.DELIVERYDISPATCH_COURIER_STREAM ?? 'ws://127.0.0.1:31086',
    courierSessionSpotEndpoint: process.env.DELIVERYDISPATCH_COURIER_SESSION_SPOT ?? 'tcp://127.0.0.1:31098',
    courierActorNode1RouteEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE ?? 'tcp://127.0.0.1:31087',
    courierActorNode2RouteEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE ?? 'tcp://127.0.0.1:31088',
    courierActorNode1SpotEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT ?? 'tcp://127.0.0.1:31089',
    courierActorNode2SpotEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT ?? 'tcp://127.0.0.1:31090',
    trackingEndpoint: process.env.DELIVERYDISPATCH_TRACKING_ROUTE ?? 'tcp://127.0.0.1:31091',
    trackingSpotEndpoint: process.env.DELIVERYDISPATCH_TRACKING_SPOT ?? 'tcp://127.0.0.1:31094',
    sessionStreamEndpoint: process.env.DELIVERYDISPATCH_SESSION_STREAM ?? 'ws://127.0.0.1:31095',
    sessionSpotRouterEndpoint: process.env.DELIVERYDISPATCH_SESSION_SPOT_ROUTER ?? 'tcp://127.0.0.1:31096',
    sessionSpotNodeRid: process.env.DELIVERYDISPATCH_SESSION_SPOT_NODE_RID ?? 'delivery-session-node',
    redisEndpoint: requireEnv('DELIVERYDISPATCH_REDIS_ENDPOINT'),
    redisKeyPrefix: process.env.DELIVERYDISPATCH_REDIS_KEY_PREFIX ?? 'deliverydispatch:node:'
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchServerConfig
};
