type DeliveryDispatchServerConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  dispatchApiHttpUrl: string;
  dispatchEndpoint: string;
  courierRouteEndpoint: string;
  courierStreamEndpoint: string;
  courierActorNode1RouteEndpoint: string;
  courierActorNode2RouteEndpoint: string;
  courierActorNode1SpotEndpoint: string;
  courierActorNode2SpotEndpoint: string;
  trackingEndpoint: string;
  statusFanoutEndpoint: string;
  trackingSpotRouterEndpoint: string;
  trackingSpotEndpoint: string;
  trackingSpotNodeRid: string;
  sessionStreamEndpoint: string;
  sessionSpotRouterEndpoint: string;
  sessionSpotEndpoint: string;
  sessionSpotNodeRid: string;
};

function loadSampleConfig(): DeliveryDispatchServerConfig {
  return {
    registryPubEndpoint: read('DELIVERYDISPATCH_REGISTRY_PUB', 'DELIVERYDISPATCH_REGISTRY_PUB_ENDPOINT', 'tcp://127.0.0.1:31081'),
    registryRouterEndpoint: read('DELIVERYDISPATCH_REGISTRY', 'DELIVERYDISPATCH_REGISTRY_ROUTER_ENDPOINT', 'tcp://127.0.0.1:31082'),
    dispatchApiHttpUrl: process.env.DELIVERYDISPATCH_API_HTTP ?? 'http://127.0.0.1:31083',
    dispatchEndpoint: read('DELIVERYDISPATCH_CENTER_ROUTE', 'DELIVERYDISPATCH_DISPATCH_ENDPOINT', 'tcp://127.0.0.1:31084'),
    courierRouteEndpoint: process.env.DELIVERYDISPATCH_COURIER_ROUTE ?? 'tcp://127.0.0.1:31085',
    courierStreamEndpoint: process.env.DELIVERYDISPATCH_COURIER_STREAM ?? 'tcp://127.0.0.1:31086',
    courierActorNode1RouteEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE ?? 'tcp://127.0.0.1:31087',
    courierActorNode2RouteEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE ?? 'tcp://127.0.0.1:31088',
    courierActorNode1SpotEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE1_SPOT ?? 'tcp://127.0.0.1:31089',
    courierActorNode2SpotEndpoint: process.env.DELIVERYDISPATCH_COURIER_ACTOR_NODE2_SPOT ?? 'tcp://127.0.0.1:31090',
    trackingEndpoint: process.env.DELIVERYDISPATCH_TRACKING_ROUTE ?? 'tcp://127.0.0.1:31091',
    statusFanoutEndpoint: process.env.DELIVERYDISPATCH_STATUS_FANOUT ?? 'tcp://127.0.0.1:31092',
    trackingSpotRouterEndpoint: process.env.DELIVERYDISPATCH_TRACKING_SPOT_ROUTER ?? 'tcp://127.0.0.1:31093',
    trackingSpotEndpoint: process.env.DELIVERYDISPATCH_TRACKING_SPOT ?? 'tcp://127.0.0.1:31094',
    trackingSpotNodeRid: process.env.DELIVERYDISPATCH_TRACKING_SPOT_NODE_RID ?? 'delivery-tracking-node',
    sessionStreamEndpoint: process.env.DELIVERYDISPATCH_SESSION_STREAM ?? 'tcp://127.0.0.1:31095',
    sessionSpotRouterEndpoint: process.env.DELIVERYDISPATCH_SESSION_SPOT_ROUTER ?? 'tcp://127.0.0.1:31096',
    sessionSpotEndpoint: process.env.DELIVERYDISPATCH_SESSION_SPOT ?? 'tcp://127.0.0.1:31097',
    sessionSpotNodeRid: process.env.DELIVERYDISPATCH_SESSION_SPOT_NODE_RID ?? 'delivery-session-node'
  };
}

function read(primary: string, legacy: string, fallback: string): string {
  return process.env[primary] ?? process.env[legacy] ?? fallback;
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchServerConfig
};
