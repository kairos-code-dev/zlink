export interface ZLinkRegistryOptions {
  pubEndpoint: string;
  routerEndpoint: string;
  registryId?: number;
  heartbeatIntervalMs?: number;
  heartbeatTimeoutMs?: number;
  broadcastIntervalMs?: number;
  peers?: readonly string[];
}
