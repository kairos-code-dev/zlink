import * as fs from 'node:fs';
type SupportChatSampleConfig = {
  registryPubEndpoint: string;
  registryRouterEndpoint: string;
  sessionEndpoint: string;
  supportEndpoint: string;
  notificationEndpoint: string;
  apiEndpoint: string;
};

function loadSampleConfig(): SupportChatSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    registryPubEndpoint: requireEnv('SUPPORTCHAT_REGISTRY_PUB_ENDPOINT'),
    registryRouterEndpoint: requireEnv('SUPPORTCHAT_REGISTRY_ROUTER_ENDPOINT'),
    sessionEndpoint: requireEnv('SUPPORTCHAT_SESSION_ENDPOINT'),
    supportEndpoint: requireEnv('SUPPORTCHAT_SUPPORT_ENDPOINT'),
    notificationEndpoint: requireEnv('SUPPORTCHAT_NOTIFICATION_ENDPOINT'),
    apiEndpoint: requireEnv('SUPPORTCHAT_API_ENDPOINT')
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

export { loadSampleConfig };
export type { SupportChatSampleConfig };
