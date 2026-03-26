export function version(): [number, number, number];

export declare const SocketType: {
  readonly PAIR: 0x1001; readonly PUB: 0x1002; readonly SUB: 0x1003;
  readonly DEALER: 0x1004; readonly ROUTER: 0x1005; readonly XPUB: 0x1006;
  readonly XSUB: 0x1007; readonly STREAM: 0x1008;
};

export declare const ContextOption: {
  readonly IO_THREADS: 1; readonly MAX_SOCKETS: 2; readonly SOCKET_LIMIT: 3;
  readonly THREAD_PRIORITY: 3; readonly THREAD_SCHED_POLICY: 4;
  readonly MAX_MSGSZ: 5; readonly MSG_T_SIZE: 6;
  readonly THREAD_AFFINITY_CPU_ADD: 7; readonly THREAD_AFFINITY_CPU_REMOVE: 8;
  readonly THREAD_NAME_PREFIX: 9; readonly BLOCKY: 10;
};

export declare const SocketOption: Readonly<Record<string, number>>;
export declare const SendFlag: { readonly NONE: 0; readonly DONTWAIT: 0x0001; readonly SNDMORE: 0x0002; };
export declare const ReceiveFlag: { readonly NONE: 0; readonly DONTWAIT: 0x0001; };
export declare const StreamDispatchMode: { readonly NONE: 0; readonly LEN32BE: 1; };
export declare const ErrorCode: Readonly<Record<string, number>>;
export declare const ProtocolError: { readonly ZMP_MALFORMED_COMMAND_HELLO: 0x10000013; };
export declare const MonitorEvent: Readonly<Record<string, number>>;
export declare const DisconnectReason: { readonly UNKNOWN: 0; readonly HANDSHAKE_FAILED: 3; readonly TRANSPORT_ERROR: 4; readonly CTX_TERM: 5; };
export declare const PollEvent: { readonly POLLIN: 1; readonly POLLOUT: 2; readonly POLLERR: 4; readonly POLLPRI: 8; };
export declare const ServiceType: { readonly SPOT: 0x3002; readonly SOCKET: 0x3003; };
export declare const SERVICE_TYPE_SPOT: 0x3002;
export declare const SERVICE_TYPE_SOCKET: 0x3003;
export declare const ServiceRole: Readonly<Record<string, number>>;
export declare const ServiceKind: Readonly<Record<string, number>>;
export declare const RegistrySocketRole: Readonly<Record<string, number>>;
export declare const DiscoverySocketRole: Readonly<Record<string, number>>;
export declare const SpotNodeSocketRole: Readonly<Record<string, number>>;
export declare const SpotNodeOption: Readonly<Record<string, number>>;
export declare const SpotNodePubMode: Readonly<Record<string, number>>;
export declare const SpotNodePubQueueFullPolicy: Readonly<Record<string, number>>;
export declare const SpotSocketRole: Readonly<Record<string, number>>;
export declare const MonitorSourceKind: Readonly<Record<string, number>>;
export declare const MonitorState: Readonly<Record<string, number>>;
export declare const MonitorSnapshotDetail: Readonly<Record<string, number>>;
export declare const ServiceMonitorEvent: Readonly<Record<string, number>>;
export declare const TopologySource: Readonly<Record<string, number>>;
export declare const TopologyState: Readonly<Record<string, number>>;

export type BufferLike = Buffer | Uint8Array;

export interface MonitorSnapshot {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
  readyCount: number;
  sndPendingMsgs: number;
  rcvPendingMsgs: number;
}

export interface SocketMonitorEventValue {
  event: number;
  value: number;
  local: string;
  remote: string;
}

export interface ServiceEventValue {
  serviceKind: number;
  eventType: number;
  status: number;
  errorCode: number;
  value: number;
  detailFlags: number;
  serviceName: string;
  endpoint: string;
  routingId: Buffer | null;
  subject: string;
  subjectKind: number;
}

export interface MemberPeerEntry {
  serviceType: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  routingId: Buffer | null;
  value: number;
}

export interface RegistryStatus {
  registryId: number;
  bindEndpoint: string;
  state: number;
  topologyEntryCount: number;
  peerRegistryCount: number;
  connectedPeerRegistryCount: number;
  listSeq: number;
  lastError: number;
  lastChangedMs: number;
}

export interface RegistryTopologyEntry {
  routingId: Buffer | null;
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  source: number;
  state: number;
  desiredCount: number;
  readyCount: number;
  errorCode: number;
  lastReportedMs: number;
}

export interface RegistryServiceSummaryEntry {
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  totalCount: number;
  connectingCount: number;
  readyCount: number;
  errorCount: number;
  stoppedCount: number;
  lastReportedMs: number;
}

export interface RegistryTopologyFilter {
  serviceKind?: number;
  serviceRole?: number;
  serviceName?: string;
  source?: number;
  state?: number;
}

export interface SpotNodeStatus {
  serviceName: string;
  localEndpoint: string;
  nodeRoutingId: Buffer | null;
  state: number;
  configuredPeerCount: number;
  activePeerCount: number;
  connectedPeerCount: number;
  subjectCount: number;
  readySubjectCount: number;
  lastError: number;
  lastChangedMs: number;
}

export interface SpotNodePeerEntry {
  serviceName: string;
  localEndpoint: string;
  peerEndpoint: string;
  source: number;
  state: number;
  connectedSinceMs: number;
  lastChangedMs: number;
}

export interface SpotNodeSubjectEntry {
  role: number;
  subject: string;
  subjectKind: number;
  readyPeerCount: number;
  activePeerCount: number;
  lastChangedMs: number;
}

export class Context {
  close(): void;
}

export class Message {
  static copyOf(data: BufferLike | string, encoding?: BufferEncoding): Message;
  static wrap(buffer: BufferLike): Message;
  static empty(): Message;
  toBuffer(): Buffer;
  byteLength(): number;
}

export class Received {
  readonly parts: readonly Buffer[];
  readonly routingId: Buffer | null;
  readonly hasMore: boolean;
  close(): void;
}

export class Socket {
  constructor(ctx: Context, type: number);
  bind(endpoint: string): void;
  connect(endpoint: string): void;
  send(message: Message | BufferLike | string, flags?: number): number;
  sendParts(parts: readonly (Message | BufferLike | string)[], flags?: number): number;
  sendFrom(buffer: BufferLike, length: number, flags?: number): number;
  recv(flags?: number): Received;
  recv(size: number, flags: number): Buffer;
  recvInto(buffer: BufferLike, flags?: number): number;
  recvMsgInto(buffer: BufferLike, flags?: number): number;
  setOption(option: number, value: BufferLike | string): void;
  getOption(option: number): Buffer;
  setRoutingId(routingId: BufferLike): void;
  getRoutingId(): Buffer;
  subscribe(filter: BufferLike | string): void;
  unsubscribe(filter: BufferLike | string): void;
  monitorOpen(events: number): MonitorSocket;
  streamAttach(handler: (routingId: Buffer, packets: Buffer[]) => number | void, mode?: number): void;
  streamDetach(): void;
  streamPeerRoutingId(index?: number): Buffer | null;
  streamSend(routingId: BufferLike, payload: BufferLike, flags?: number): number;
  close(): void;
}

export class MonitorSocket {
  recv(): SocketMonitorEventValue;
  snapshot(): MonitorSnapshot;
  close(): void;
}

export class ServiceMonitor {
  recv(): ServiceEventValue;
  snapshot(): MonitorSnapshot;
  close(): void;
}

export class Poller {
  addSocket(socket: Socket, events: number): void;
  poll(timeoutMs: number): number[];
}

export class Registry {
  constructor(ctx: Context);
  bind(pub: string, router: string): void;
  setId(id: number): void;
  addPeer(pub: string): void;
  setHeartbeat(intervalMs: number, timeoutMs: number): void;
  setBroadcastInterval(intervalMs: number): void;
  statusSnapshot(): RegistryStatus;
  serviceSummarySnapshot(): RegistryServiceSummaryEntry[];
  topologySnapshot(): RegistryTopologyEntry[];
  topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  memberPeers(serviceType: number, serviceName?: string): MemberPeerEntry[];
  close(): void;
}

export class RegistryQueryClient {
  constructor(ctx: Context);
  connect(endpoint: string): void;
  snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[];
  close(): void;
}

export class Discovery {
  constructor(ctx: Context, serviceType: number, serviceName: string);
  readonly serviceType: number;
  readonly serviceName: string;
  connectRegistry(endpoint: string): void;
  receiverCount(): number;
  setValue(value: number): void;
  value(): number;
  setMetadata(metadata: BufferLike | string): void;
  metadata(): Buffer;
  memberPeers(): MemberPeerEntry[];
  serviceAvailable(): boolean;
  openMonitor(events?: number): ServiceMonitor;
  setTlsClient(caCert: string, hostname: string, trustSystem?: number): void;
  close(): void;
}

export class SpotNode {
  constructor(ctx: Context);
  bind(endpoint: string): void;
  connectPeerPub(endpoint: string): void;
  disconnectPeerPub(endpoint: string): void;
  attachDiscovery(discovery: Discovery): void;
  register(): never;
  unregister(): never;
  setTlsServer(cert: string, key: string, requireClient?: number): void;
  setTlsClient(ca: string, host: string, trust?: number): void;
  statusSnapshot(): SpotNodeStatus;
  peersSnapshot(): SpotNodePeerEntry[];
  subjectsSnapshot(): SpotNodeSubjectEntry[];
  openMonitor(events?: number): ServiceMonitor;
  close(): void;
}

export class Spot {
  constructor(ctx: Context);
  publish(topic: string, payloadOrParts: Message | BufferLike | string | readonly (Message | BufferLike | string)[], flags?: number): void;
  subscribe(topic: string): void;
  subscribePattern(pattern: string): void;
  unsubscribe(topicOrPattern: string): void;
  recv(flags?: number): { topic: string; parts: Buffer[] };
  openMonitor(events?: number): ServiceMonitor;
  close(): void;
}
