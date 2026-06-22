type SampleConfig = {
  registryRouterEndpoint: string;
  workflowEndpoint: string;
};

function loadSampleConfig(): SampleConfig {
  return {
    registryRouterEndpoint: process.env.SHOPPINGMALL_REGISTRY_ROUTER_ENDPOINT ?? 'tcp://127.0.0.1:31086',
    workflowEndpoint: process.env.SHOPPINGMALL_WORKFLOW_ENDPOINT ?? 'tcp://127.0.0.1:31093'
  };
}

export {
  loadSampleConfig
};

export type {
  SampleConfig
};
