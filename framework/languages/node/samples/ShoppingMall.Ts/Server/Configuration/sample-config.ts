type SampleConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  apiAHttpUrl: string;
  apiBHttpUrl: string;
  workflowAEndpoint: string;
  workflowBEndpoint: string;
  storeDirectory: string;
};

function loadSampleConfig(): SampleConfig {
  return {
    registryPubEndpoint: process.env.SHOPPINGMALL_REGISTRY_PUB_ENDPOINT ?? 'tcp://127.0.0.1:31085',
    registryRouterEndpoint: process.env.SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT ?? 'tcp://127.0.0.1:31086',
    apiAHttpUrl: process.env.SHOPPINGMALL_API_A_HTTP ?? 'http://127.0.0.1:31087',
    apiBHttpUrl: process.env.SHOPPINGMALL_API_B_HTTP ?? 'http://127.0.0.1:31088',
    workflowAEndpoint: process.env.SHOPPINGMALL_WORKFLOW_A_ENDPOINT ?? 'tcp://127.0.0.1:31093',
    workflowBEndpoint: process.env.SHOPPINGMALL_WORKFLOW_B_ENDPOINT ?? 'tcp://127.0.0.1:31094',
    storeDirectory: process.env.SHOPPINGMALL_STORE_DIR ?? '.shoppingmall-store'
  };
}

export {
  loadSampleConfig
};

export type {
  SampleConfig
};
