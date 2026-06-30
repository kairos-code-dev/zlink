type DeliveryDispatchServerConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  dispatchApiHttpUrl: string;
  dispatchEndpoint: string;
  courierAEndpoint: string;
  courierBEndpoint: string;
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
    courierAEndpoint: process.env.DELIVERYDISPATCH_COURIER_A_ROUTE ?? 'tcp://127.0.0.1:31085',
    courierBEndpoint: process.env.DELIVERYDISPATCH_COURIER_B_ROUTE ?? 'tcp://127.0.0.1:31086',
    trackingEndpoint: process.env.DELIVERYDISPATCH_TRACKING_ROUTE ?? 'tcp://127.0.0.1:31087',
    statusFanoutEndpoint: process.env.DELIVERYDISPATCH_STATUS_FANOUT ?? 'tcp://127.0.0.1:31088',
    trackingSpotRouterEndpoint: process.env.DELIVERYDISPATCH_TRACKING_SPOT_ROUTER ?? 'tcp://127.0.0.1:31089',
    trackingSpotEndpoint: process.env.DELIVERYDISPATCH_TRACKING_SPOT ?? 'tcp://127.0.0.1:31090',
    trackingSpotNodeRid: process.env.DELIVERYDISPATCH_TRACKING_SPOT_NODE_RID ?? 'delivery-tracking-node',
    sessionStreamEndpoint: process.env.DELIVERYDISPATCH_SESSION_STREAM ?? 'tcp://127.0.0.1:31091',
    sessionSpotRouterEndpoint: process.env.DELIVERYDISPATCH_SESSION_SPOT_ROUTER ?? 'tcp://127.0.0.1:31092',
    sessionSpotEndpoint: process.env.DELIVERYDISPATCH_SESSION_SPOT ?? 'tcp://127.0.0.1:31093',
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
