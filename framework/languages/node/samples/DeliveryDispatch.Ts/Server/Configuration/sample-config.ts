type DeliveryDispatchServerConfig = {
  dispatchEndpoint: string;
};

function loadSampleConfig(): DeliveryDispatchServerConfig {
  return {
    dispatchEndpoint: process.env.DELIVERYDISPATCH_DISPATCH_ENDPOINT ?? 'tcp://127.0.0.1:31091'
  };
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchServerConfig
};
