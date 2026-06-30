type DeliveryDispatchClientConfig = {
  dispatchApiHttpUrl: string;
  sessionStreamEndpoint: string;
};

function loadSampleConfig(): DeliveryDispatchClientConfig {
  return {
    dispatchApiHttpUrl: process.env.DELIVERYDISPATCH_API_HTTP ?? 'http://127.0.0.1:31083',
    sessionStreamEndpoint: process.env.DELIVERYDISPATCH_SESSION_STREAM ?? 'tcp://127.0.0.1:31091'
  };
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchClientConfig
};
