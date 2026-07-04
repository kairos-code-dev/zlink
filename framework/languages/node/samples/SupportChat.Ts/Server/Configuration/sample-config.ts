type SupportChatServerConfig = {
  apiHttpUrl: string;
  apiChannelEndpoint: string;
  supportChannelEndpoint: string;
  sessionStreamEndpoint: string;
  registryEndpoint: string;
  stateFile: string;
};

function loadSampleConfig(): SupportChatServerConfig {
  const runDir = process.env.SUPPORTCHAT_WORK_DIR ?? process.cwd();
  return {
    apiHttpUrl: process.env.SUPPORTCHAT_API_HTTP ?? 'http://127.0.0.1:31180',
    apiChannelEndpoint: process.env.SUPPORTCHAT_API_CHANNEL_ENDPOINT ?? 'tcp://127.0.0.1:31181',
    supportChannelEndpoint: process.env.SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT ?? 'tcp://127.0.0.1:31182',
    sessionStreamEndpoint: process.env.SUPPORTCHAT_STREAM_ENDPOINT ?? 'tcp://127.0.0.1:31183',
    registryEndpoint: process.env.SUPPORTCHAT_REGISTRY_ENDPOINT ?? 'tcp://127.0.0.1:31184',
    stateFile: process.env.SUPPORTCHAT_STATE_FILE ?? `${runDir}/supportchat-state.json`
  };
}

export { loadSampleConfig };
export type { SupportChatServerConfig };
