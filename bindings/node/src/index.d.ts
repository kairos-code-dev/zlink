export function version(): [number, number, number];

export class Context {
  close(): void;
}

export class Socket {
  constructor(ctx: Context, type: number);
  bind(endpoint: string): void;
  connect(endpoint: string): void;
  send(buf: Buffer | Uint8Array | string, flags?: number): number;
  recv(size: number, flags?: number): Buffer;
  setSockOpt(option: number, value: Buffer | Uint8Array | string): void;
  getSockOpt(option: number): Buffer;
  monitorOpen(events: number): MonitorSocket;
  close(): void;
}

export class MonitorSocket {
  recv(flags?: number): { event: number; value: number; local: string; remote: string };
  close(): void;
}

export class Poller {
  addSocket(socket: Socket, events: number): void;
  poll(timeoutMs: number): number[];
}

export class Registry {
  constructor(ctx: Context);
  setEndpoints(pub: string, router: string): void;
  setId(id: number): void;
  addPeer(pub: string): void;
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  setBroadcastInterval(intervalMs: number): void;
  start(): void;
  setSockOpt(role: number, option: number, value: Buffer | Uint8Array | string): void;
  close(): void;
}

export class Discovery {
  constructor(ctx: Context);
  connectRegistry(pub: string): void;
  subscribe(service: string): void;
  unsubscribe(service: string): void;
  providerCount(service: string): number;
  serviceAvailable(service: string): boolean;
  getProviders(service: string): Array<{ serviceName: string; endpoint: string; weight: number; registeredAt: number }>;
  setSockOpt(role: number, option: number, value: Buffer | Uint8Array | string): void;
  close(): void;
}

export class Gateway {
  constructor(ctx: Context, discovery: Discovery, routingId?: string | null);
  send(service: string, parts: Buffer[], flags?: number): void;
  recv(flags?: number): { service: string; parts: Buffer[] };
  setLoadBalancing(service: string, strategy: number): void;
  setTlsClient(ca: string, host: string, trust: number): void;
  connectionCount(service: string): number;
  setSockOpt(option: number, value: Buffer | Uint8Array | string): void;
  close(): void;
}

export class Provider {
  constructor(ctx: Context, routingId?: string | null);
  bind(endpoint: string): void;
  connectRegistry(endpoint: string): void;
  register(service: string, endpoint: string, weight: number): void;
  updateWeight(service: string, weight: number): void;
  unregister(service: string): void;
  registerResult(service: string): { status: number; resolvedEndpoint: string; errorMessage: string };
  setTlsServer(cert: string, key: string): void;
  setSockOpt(role: number, option: number, value: Buffer | Uint8Array | string): void;
  routerSocket(): Socket;
  close(): void;
}

export class SpotNode {
  constructor(ctx: Context);
  bind(endpoint: string): void;
  connectRegistry(endpoint: string): void;
  connectPeerPub(endpoint: string): void;
  disconnectPeerPub(endpoint: string): void;
  register(service: string, endpoint: string): void;
  unregister(service: string): void;
  setDiscovery(discovery: Discovery, service: string): void;
  setTlsServer(cert: string, key: string): void;
  setTlsClient(ca: string, host: string, trust: number): void;
  pubSocket(): Socket;
  subSocket(): Socket;
  close(): void;
}

export class Spot {
  constructor(node: SpotNode);
  topicCreate(topic: string, mode: number): void;
  topicDestroy(topic: string): void;
  publish(topic: string, parts: Buffer[], flags?: number): void;
  subscribe(topic: string): void;
  subscribePattern(pattern: string): void;
  unsubscribe(topicOrPattern: string): void;
  recv(flags?: number): { topic: string; parts: Buffer[] };
  pubSocket(): Socket;
  subSocket(): Socket;
  close(): void;
}
