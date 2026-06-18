type SampleConfig = {
  checkoutEndpoint: string;
};

function loadSampleConfig(): SampleConfig {
  return {
    checkoutEndpoint: process.env.SHOPPINGMALLCHECKOUT_CHECKOUT_ENDPOINT ?? 'tcp://127.0.0.1:31094'
  };
}

export {
  loadSampleConfig
};

export type {
  SampleConfig
};
