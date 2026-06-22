type DeliveryDispatchClientConfig = {
  registryRouterEndpoint: string;
  dispatchEndpoint: string;
};

function loadSampleConfig(): DeliveryDispatchClientConfig {
  return {
    registryRouterEndpoint: process.env.DELIVERYDISPATCH_REGISTRY_ROUTER_ENDPOINT ?? 'tcp://127.0.0.1:31082',
    dispatchEndpoint: process.env.DELIVERYDISPATCH_DISPATCH_ENDPOINT ?? 'tcp://127.0.0.1:31091'
  };
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchClientConfig
};
