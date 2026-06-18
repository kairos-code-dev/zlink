type SampleConfig = {
  workflowEndpoint: string;
};

function loadSampleConfig(): SampleConfig {
  return {
    workflowEndpoint: process.env.SHOPPINGMALL_WORKFLOW_ENDPOINT ?? 'tcp://127.0.0.1:31093'
  };
}

export {
  loadSampleConfig
};

export type {
  SampleConfig
};
