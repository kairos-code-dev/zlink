type DeliveryDispatchClientConfig = {
  dispatchApiHttpUrl: string;
  sessionStreamEndpoint: string;
  courierStreamEndpoint: string;
};

function loadSampleConfig(): DeliveryDispatchClientConfig {
  return {
    dispatchApiHttpUrl: process.env.DELIVERYDISPATCH_API_HTTP ?? 'http://127.0.0.1:31083',
    sessionStreamEndpoint: process.env.DELIVERYDISPATCH_SESSION_STREAM ?? 'tcp://127.0.0.1:31093',
    courierStreamEndpoint: process.env.DELIVERYDISPATCH_COURIER_STREAM ?? 'tcp://127.0.0.1:31086'
  };
}

export {
  loadSampleConfig
};

export type {
  DeliveryDispatchClientConfig
};
