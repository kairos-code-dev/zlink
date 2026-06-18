type DeliveryDispatchClientConfig = {
  dispatchEndpoint: string;
};

function loadSampleConfig(): DeliveryDispatchClientConfig {
  return {
    dispatchEndpoint: process.env.DELIVERYDISPATCH_DISPATCH_ENDPOINT ?? 'tcp://127.0.0.1:31091'
  };
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchClientConfig
};
