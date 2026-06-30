type SampleConfig = {
  apiAHttpUrl: string;
  apiBHttpUrl: string;
};

function loadSampleConfig(): SampleConfig {
  return {
    apiAHttpUrl: process.env.SHOPPINGMALL_API_A_HTTP ?? 'http://127.0.0.1:31087',
    apiBHttpUrl: process.env.SHOPPINGMALL_API_B_HTTP ?? 'http://127.0.0.1:31088'
  };
}

export {
  loadSampleConfig
};

export type {
  SampleConfig
};
