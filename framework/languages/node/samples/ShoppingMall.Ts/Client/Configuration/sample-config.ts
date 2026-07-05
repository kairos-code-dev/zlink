interface ShoppingMallSampleConfig {
  apiAHttpUrl: string;
  apiBHttpUrl: string;
}

function loadSampleConfig(): ShoppingMallSampleConfig {
  return {
    apiAHttpUrl: process.env.SHOPPINGMALL_API_A_HTTP ?? 'http://127.0.0.1:45121',
    apiBHttpUrl: process.env.SHOPPINGMALL_API_B_HTTP ?? 'http://127.0.0.1:45122'
  };
}

export { loadSampleConfig };
export type { ShoppingMallSampleConfig };
