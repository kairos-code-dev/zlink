export function version(): [number, number, number];

export declare const SocketType: {
  readonly PAIR: 0; readonly PUB: 1; readonly SUB: 2;
  readonly DEALER: 5; readonly ROUTER: 6; readonly XPUB: 9;
  readonly XSUB: 10; readonly STREAM: 11;
};

export declare const ContextOption: {
  readonly IO_THREADS: 1; readonly MAX_SOCKETS: 2;
  readonly SOCKET_LIMIT: 3; readonly THREAD_PRIORITY: 3;
  readonly THREAD_SCHED_POLICY: 4; readonly MAX_MSGSZ: 5;
  readonly MSG_T_SIZE: 6; readonly THREAD_AFFINITY_CPU_ADD: 7;
  readonly THREAD_AFFINITY_CPU_REMOVE: 8; readonly THREAD_NAME_PREFIX: 9;
};

export declare const SocketOption: {
  readonly AFFINITY: 4; readonly ROUTING_ID: 5;
  readonly SUBSCRIBE: 6; readonly UNSUBSCRIBE: 7;
  readonly RATE: 8; readonly RECOVERY_IVL: 9;
  readonly SNDBUF: 11; readonly RCVBUF: 12;
  readonly RCVMORE: 13; readonly FD: 14;
  readonly EVENTS: 15; readonly TYPE: 16;
  readonly LINGER: 17; readonly RECONNECT_IVL: 18;
  readonly BACKLOG: 19; readonly RECONNECT_IVL_MAX: 21;
  readonly MAXMSGSIZE: 22; readonly SNDHWM: 23;
  readonly RCVHWM: 24; readonly MULTICAST_HOPS: 25;
  readonly RCVTIMEO: 27; readonly SNDTIMEO: 28;
  readonly LAST_ENDPOINT: 32; readonly ROUTER_MANDATORY: 33;
  readonly TCP_KEEPALIVE: 34; readonly TCP_KEEPALIVE_CNT: 35;
  readonly TCP_KEEPALIVE_IDLE: 36; readonly TCP_KEEPALIVE_INTVL: 37;
  readonly IMMEDIATE: 39; readonly XPUB_VERBOSE: 40;
  readonly IPV6: 42; readonly PROBE_ROUTER: 51;
  readonly CONFLATE: 54; readonly ROUTER_HANDOVER: 56;
  readonly TOS: 57; readonly CONNECT_ROUTING_ID: 61;
  readonly HANDSHAKE_IVL: 66; readonly XPUB_NODROP: 69;
  readonly BLOCKY: 70; readonly XPUB_MANUAL: 71;
  readonly XPUB_WELCOME_MSG: 72; readonly INVERT_MATCHING: 74;
  readonly HEARTBEAT_IVL: 75; readonly HEARTBEAT_TTL: 76;
  readonly HEARTBEAT_TIMEOUT: 77; readonly XPUB_VERBOSER: 78;
  readonly CONNECT_TIMEOUT: 79; readonly TCP_MAXRT: 80;
  readonly MULTICAST_MAXTPDU: 84; readonly USE_FD: 89;
  readonly REQUEST_TIMEOUT: 90; readonly REQUEST_CORRELATE: 91;
  readonly BINDTODEVICE: 92; readonly TLS_CERT: 95;
  readonly TLS_KEY: 96; readonly TLS_CA: 97;
  readonly TLS_VERIFY: 98; readonly TLS_REQUIRE_CLIENT_CERT: 99;
  readonly TLS_HOSTNAME: 100; readonly TLS_TRUST_SYSTEM: 101;
  readonly TLS_PASSWORD: 102; readonly XPUB_MANUAL_LAST_VALUE: 98;
  readonly ONLY_FIRST_SUBSCRIBE: 108; readonly TOPICS_COUNT: 116;
  readonly ZMP_METADATA: 117;
};

export declare const SendFlag: {
  readonly NONE: 0; readonly DONTWAIT: 1; readonly SNDMORE: 2;
};

export declare const ReceiveFlag: {
  readonly NONE: 0; readonly DONTWAIT: 1;
};

export declare const MonitorEvent: {
  readonly CONNECTED: 0x0001; readonly CONNECT_DELAYED: 0x0002;
  readonly CONNECT_RETRIED: 0x0004; readonly LISTENING: 0x0008;
  readonly BIND_FAILED: 0x0010; readonly ACCEPTED: 0x0020;
  readonly ACCEPT_FAILED: 0x0040; readonly CLOSED: 0x0080;
  readonly CLOSE_FAILED: 0x0100; readonly DISCONNECTED: 0x0200;
  readonly MONITOR_STOPPED: 0x0400;
  readonly HANDSHAKE_FAILED_NO_DETAIL: 0x0800;
  readonly CONNECTION_READY: 0x1000;
  readonly HANDSHAKE_FAILED_PROTOCOL: 0x2000;
  readonly HANDSHAKE_FAILED_AUTH: 0x4000;
  readonly ALL: 0xFFFF;
};

export declare const DisconnectReason: {
  readonly UNKNOWN: 0; readonly LOCAL: 1; readonly REMOTE: 2;
  readonly HANDSHAKE_FAILED: 3; readonly TRANSPORT_ERROR: 4;
  readonly CTX_TERM: 5;
};

export declare const PollEvent: {
  readonly POLLIN: 1; readonly POLLOUT: 2;
  readonly POLLERR: 4; readonly POLLPRI: 8;
};

export declare const ServiceType: {
  readonly GATEWAY: 1; readonly SPOT: 2;
};

// Backward-compatible aliases
export declare const SERVICE_TYPE_GATEWAY: 1;
export declare const SERVICE_TYPE_SPOT: 2;

export declare const GatewayLbStrategy: {
  readonly ROUND_ROBIN: 0; readonly WEIGHTED: 1;
};

export declare const SpotTopicMode: {
  readonly QUEUE: 0; readonly RINGBUFFER: 1;
};

export declare const RegistrySocketRole: {
  readonly PUB: 1; readonly ROUTER: 2; readonly PEER_SUB: 3;
};

export declare const DiscoverySocketRole: {
  readonly SUB: 1;
};

export declare const GatewaySocketRole: {
  readonly ROUTER: 1;
};

export declare const ReceiverSocketRole: {
  readonly ROUTER: 1; readonly DEALER: 2;
};

export declare const SpotNodeSocketRole: {
  readonly PUB: 1; readonly SUB: 2; readonly DEALER: 3;
};

export declare const SpotSocketRole: {
  readonly PUB: 1; readonly SUB: 2;
};

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
  constructor(ctx: Context, serviceType: number);
  connectRegistry(pub: string): void;
  subscribe(service: string): void;
  unsubscribe(service: string): void;
  receiverCount(service: string): number;
  getReceivers(service: string): Array<{ serviceName: string; endpoint: string; weight: number; registeredAt: number }>;
  serviceAvailable(service: string): boolean;
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

export class Receiver {
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
  publish(topic: string, parts: Buffer[], flags?: number): void;
  subscribe(topic: string): void;
  subscribePattern(pattern: string): void;
  unsubscribe(topicOrPattern: string): void;
  recv(flags?: number): { topic: string; parts: Buffer[] };
  pubSocket(): Socket;
  subSocket(): Socket;
  close(): void;
}
