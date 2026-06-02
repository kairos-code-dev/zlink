import type {
  Message,
  RoutingId,
  Type,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkPublishCall,
  ZLinkRequestCall,
  ZLinkSendCall,
  ZLinkSpot,
  ZLinkSpotContext,
  ZLinkSpotCreateResult,
  ZLinkSpotHandlerRegistry,
  ZLinkSpotInfo,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotRemoteAddress,
  ZLinkSpotRemoteAddressResolver,
  ZLinkSpotTimerHandler,
  ZLinkTimer,
  ZLinkTimerOptions,
  ZLinkTimerTick
} from '../../contracts';
import { ZLinkAutoConnectType, ZLinkTimerOverrunPolicy } from '../../contracts';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration,
  type ZLinkSpotNodeOptions
} from '../configuration';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkChannelBackendAdapter,
  ZLinkBackendDiscovery,
  ZLinkBackendContext,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { ZLINK_BACKEND_SPOT_NODE_MODE_ALL } from '../backend/contracts';

export interface ZLinkSpotManagerOptions {
  readonly spotFactories: readonly Type<ZLinkSpot>[];
  readonly nodeRid?: RoutingId;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
}

export interface ZLinkSpotNodeRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
}

export class ZLinkSpotNodeRuntimeManager {
  private readonly nodes = new Map<string, ZLinkBackendSpotNode>();
  private readonly ownedObjects: ZLinkOwnedBackendObject[] = [];

  constructor(private readonly options: ZLinkSpotNodeRuntimeManagerOptions) {}

  start(): void {
    if (this.options.registration.spotNodes.size === 0) {
      return;
    }
    const spotAdapter = this.options.backendAdapterFactory.createSpotAdapter();
    const channelAdapter = this.options.backendAdapterFactory.createChannelAdapter();
    for (const [spotNodeName, spotNode] of this.options.registration.spotNodes.entries()) {
      const node = spotAdapter.createSpotNode(this.options.context, ZLINK_BACKEND_SPOT_NODE_MODE_ALL);
      this.applySpotNodeOptions(node, spotNode);
      this.attachChannelClients(channelAdapter, node, spotNode);
      this.attachSpotRouteChannels(channelAdapter, node, spotNode);
      this.initializeSpotPublisherClients(node, spotNode);
      this.nodes.set(spotNodeName, node);
    }
  }

  get nodesByName(): ReadonlyMap<string, ZLinkBackendSpotNode> {
    return this.nodes;
  }

  async dispose(): Promise<void> {
    const nodes = [...this.nodes.values()];
    const ownedObjects = [...this.ownedObjects];
    this.nodes.clear();
    this.ownedObjects.length = 0;
    for (const object of ownedObjects.reverse()) {
      await object.dispose();
    }
    for (const node of nodes.reverse()) {
      await node.dispose();
    }
  }

  private applySpotNodeOptions(node: ZLinkBackendSpotNode, spotNode: ZLinkSpotNodeOptions): void {
    const routingId = spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
    if (routingId !== undefined) {
      node.setRoutingId(routingId);
    }
    if (spotNode.router?.bind !== undefined) {
      node.setRouterBind(spotNode.router.bind);
    }
    if (spotNode.pubSub?.bind !== undefined) {
      node.setPubBind(spotNode.pubSub.bind);
    }
    for (const endpoint of spotNode.router?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
    for (const endpoint of spotNode.pubSub?.manualConnections ?? []) {
      node.connectPeer(endpoint);
    }
  }

  private attachChannelClients(
    channelAdapter: ZLinkChannelBackendAdapter,
    node: ZLinkBackendSpotNode,
    spotNode: ZLinkSpotNodeOptions
  ): void {
    for (const [channelName, attached] of Object.entries(spotNode.attachedChannelClients ?? {})) {
      const dealer = channelAdapter.createDealerSocket(this.options.context);
      dealer.setChannelName(channelName);
      this.ownedObjects.push(dealer);
      if ((attached.manualConnections ?? []).length > 0) {
        for (const endpoint of attached.manualConnections ?? []) {
          dealer.connect(endpoint);
        }
        node.attachChannelDealerManual(channelName, dealer);
        continue;
      }
      const discovery = this.createDiscovery(channelAdapter, channelName, ZLinkAutoConnectType.ClientServer);
      dealer.attachDiscovery(discovery);
      node.attachChannelDealer(discovery, dealer);
    }
  }

  private attachSpotRouteChannels(
    channelAdapter: ZLinkChannelBackendAdapter,
    node: ZLinkBackendSpotNode,
    spotNode: ZLinkSpotNodeOptions
  ): void {
    for (const [channelName, acceptance] of Object.entries(spotNode.acceptedSpotRouteChannels ?? {})) {
      if ((acceptance.manualConnections ?? []).length > 0) {
        for (const endpoint of acceptance.manualConnections ?? []) {
          node.connectRouterChannelPeer(channelName, endpoint);
        }
        continue;
      }
      const discovery = this.createDiscovery(channelAdapter, channelName, this.resolveSpotRouteAutoConnectType(channelName));
      node.attachSpotRouteChannelDiscovery(channelName, discovery);
    }
  }

  private initializeSpotPublisherClients(node: ZLinkBackendSpotNode, spotNode: ZLinkSpotNodeOptions): void {
    for (const attached of Object.values(spotNode.attachedSpotPublisherClients ?? {})) {
      const publisher = node.createSpot();
      this.ownedObjects.push(publisher);
      for (const endpoint of attached.manualConnections ?? []) {
        node.connectPeer(endpoint);
      }
    }
  }

  private createDiscovery(
    channelAdapter: ZLinkChannelBackendAdapter,
    channelName: string,
    autoConnectType: ZLinkAutoConnectType
  ): ZLinkBackendDiscovery {
    const discovery = channelAdapter.createDiscovery(this.options.context, autoConnectType, channelName);
    for (const endpoint of this.options.registration.discovery?.registries ?? []) {
      discovery.connectRegistry(endpoint);
    }
    this.ownedObjects.push(discovery);
    return discovery;
  }

  private resolveSpotRouteAutoConnectType(channelName: string): ZLinkAutoConnectType {
    if (this.options.registration.routeChannelOptions.has(channelName)) {
      return ZLinkAutoConnectType.RouteMesh;
    }
    return ZLinkAutoConnectType.ClientServer;
  }
}

interface ZLinkOwnedBackendObject {
  dispose(): Promise<void>;
}

interface SpotActivation {
  readonly spotRid: RoutingId;
  readonly spotType: Type<ZLinkSpot>;
  readonly spot: ZLinkSpot;
  readonly serial: ZLinkSpotSerialExecutor;
  readonly timers: ZLinkSpotTimerRegistry;
}

interface PendingSpotActivation {
  readonly spotType: Type<ZLinkSpot>;
  readonly ready: Promise<void>;
}

export class DefaultZLinkSpotManager implements ZLinkSpotManager {
  private nextId = 1;
  private readonly factories: ReadonlySet<Type<ZLinkSpot>>;
  private readonly activations = new Map<RoutingId, SpotActivation>();
  private readonly pending = new Map<RoutingId, PendingSpotActivation>();

  constructor(private readonly options: ZLinkSpotManagerOptions) {
    this.factories = new Set(options.spotFactories);
  }

  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    createParts: readonly Message[] = [],
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const spotRid = this.allocateSpotRid();
    await this.createActivation(spotType, spotRid, createParts, signal);
    return { spotRid, created: true };
  }

  async getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    createParts: readonly Message[] = [],
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const existing = this.activations.get(spotRid);
    if (existing !== undefined) {
      if (existing.spotType !== spotType) {
        throw new ZLinkConfigurationException(`Spot '${spotRid}' already exists with a different spot type.`);
      }
      return { spotRid, created: false };
    }

    const pending = this.pending.get(spotRid);
    if (pending !== undefined) {
      if (pending.spotType !== spotType) {
        throw new ZLinkConfigurationException(`Spot '${spotRid}' is being created with a different spot type.`);
      }
      await pending.ready;
      return { spotRid, created: false };
    }

    const ready = Promise.resolve().then(() => this.createActivation(spotType, spotRid, createParts, signal));
    this.pending.set(spotRid, { spotType, ready });
    try {
      await ready;
    } finally {
      this.pending.delete(spotRid);
    }
    return { spotRid, created: true };
  }

  async find(spotRid: RoutingId): Promise<ZLinkSpotInfo | null> {
    return this.activations.has(spotRid) ? { spotRid } : null;
  }

  async list(): Promise<readonly ZLinkSpotInfo[]> {
    return [...this.activations.keys()]
      .sort((left, right) => left.localeCompare(right))
      .map((spotRid) => ({ spotRid }));
  }

  async remove(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean> {
    const activation = this.activations.get(spotRid);
    if (activation === undefined) {
      return false;
    }
    this.activations.delete(spotRid);
    await this.closeActivation(activation, signal);
    return true;
  }

  async executeOnSpot<T>(spotRid: RoutingId, operation: (spot: ZLinkSpot) => Promise<T> | T): Promise<T> {
    const activation = this.activations.get(spotRid);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    return activation.serial.execute(() => operation(activation.spot));
  }

  private async createActivation<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    createParts: readonly Message[],
    signal?: AbortSignal
  ): Promise<void> {
    this.requireRegisteredFactory(spotType);
    const serial = new ZLinkSpotSerialExecutor();
    const handlers = new DefaultZLinkSpotHandlerRegistry();
    const timers = new ZLinkSpotTimerRegistry();
    const outbound = new DefaultZLinkSpotOutbound(
      serial,
      this.options.channelClient,
      this.options.fanoutClient,
      this.options.remoteAddressResolver,
      this.options.routedTransport
    );
    let spot: ZLinkSpot | undefined;
    const context = this.createSpotContext(spotRid, handlers, outbound, timers, serial, () => spot);
    spot = new (spotType as new (context: ZLinkSpotContext) => TSpot)(context);
    Object.defineProperty(spot, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });

    const activation: SpotActivation = { spotRid, spotType, spot, serial, timers };

    try {
      spot.configure?.();
      await serial.execute(async () => {
        await spot.onCreate?.(createParts, signal);
        await spot.onInitialize?.(signal);
      });
      this.activations.set(spotRid, activation);
    } catch (error) {
      await this.closeActivation(activation, signal);
      throw error;
    }
  }

  private createSpotContext(
    spotRid: RoutingId,
    handlers: ZLinkSpotHandlerRegistry,
    outbound: ZLinkSpotOutbound,
    timers: ZLinkSpotTimerRegistry,
    serial: ZLinkSpotSerialExecutor,
    getSpot: () => ZLinkSpot | undefined
  ): ZLinkSpotContext {
    return {
      spotRid,
      nodeRid: this.options.nodeRid ?? '',
      routingId: spotRid,
      handlers,
      outbound,
      async leaveActor() {
        throw new ZLinkConfigurationException('Spot actor runtime is not started.');
      },
      addTimer: <THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
        name: string,
        periodMs: number,
        handlerType: Type<THandler>,
        options?: ZLinkTimerOptions,
        signal?: AbortSignal
      ) => {
        const spot = getSpot();
        if (spot === undefined) {
          throw new ZLinkConfigurationException('Spot timer cannot be registered before spot activation.');
        }
        return timers.add(name, periodMs, options, handlerType, serial, spot, signal);
      }
    };
  }

  private async closeActivation(activation: SpotActivation, signal?: AbortSignal): Promise<void> {
    await activation.serial.execute(async () => {
      try {
        await activation.spot.onClosing?.(signal);
      } finally {
        await activation.timers.dispose();
      }
    });
  }

  private requireRegisteredFactory(spotType: Type<ZLinkSpot>): void {
    if (!this.factories.has(spotType)) {
      throw new ZLinkConfigurationException('Spot type is not registered as a spot factory.');
    }
  }

  private allocateSpotRid(): RoutingId {
    let spotRid: RoutingId;
    do {
      spotRid = `spot-${this.nextId}`;
      this.nextId += 1;
    } while (this.activations.has(spotRid));
    return spotRid;
  }
}

export interface ZLinkSpotHandlerRegistration {
  readonly kind:
    | 'handler'
    | 'packet'
    | 'subscribe'
    | 'actorPacket'
    | 'postActorJoined'
    | 'actorLeft'
    | 'actorDisconnected'
    | 'actorJoin'
    | 'spotHandler';
  readonly handlerType: Type;
  readonly packetName?: string;
  readonly topic?: string;
  readonly actorType?: Type;
}

export class DefaultZLinkSpotHandlerRegistry implements ZLinkSpotHandlerRegistry {
  private readonly entries: ZLinkSpotHandlerRegistration[] = [];

  addHandler(handlerType: Type): this {
    this.entries.push({ kind: 'handler', handlerType });
    return this;
  }

  addPacket(handlerType: Type, packetName?: string): this {
    this.entries.push({ kind: 'packet', handlerType, packetName });
    return this;
  }

  addActorPacket(handlerType: Type, actorType: Type, packetName?: string): this {
    this.entries.push({ kind: 'actorPacket', handlerType, actorType, packetName });
    return this;
  }

  addPostActorJoined(handlerType: Type, actorType: Type): this {
    this.entries.push({ kind: 'postActorJoined', handlerType, actorType });
    return this;
  }

  addActorLeft(handlerType: Type, actorType: Type): this {
    this.entries.push({ kind: 'actorLeft', handlerType, actorType });
    return this;
  }

  addActorDisconnected(handlerType: Type, actorType: Type): this {
    this.entries.push({ kind: 'actorDisconnected', handlerType, actorType });
    return this;
  }

  addSubscribe(handlerType: Type, topic: string): this {
    if (topic.trim().length === 0) {
      throw new ZLinkConfigurationException('SPOT subscribe topic must not be empty.');
    }
    this.entries.push({ kind: 'subscribe', handlerType, topic });
    return this;
  }

  addActorJoin(handlerType: Type, actorType?: Type): this {
    this.entries.push({ kind: 'actorJoin', handlerType, actorType });
    return this;
  }

  addSpotHandler(handlerType: Type): this {
    this.entries.push({ kind: 'spotHandler', handlerType });
    return this;
  }

  snapshot(): readonly ZLinkSpotHandlerRegistration[] {
    return [...this.entries];
  }
}

export class ZLinkSpotTimerRegistry {
  private readonly timers = new Set<ZLinkTimer>();

  async add<THandler extends ZLinkSpotTimerHandler<ZLinkSpot>>(
    name: string,
    periodMs: number,
    options: ZLinkTimerOptions | undefined,
    handlerType: Type<THandler>,
    serial: ZLinkSpotSerialExecutor,
    spot: ZLinkSpot,
    signal?: AbortSignal
  ): Promise<ZLinkTimer> {
    validateTimer(name, periodMs, options);
    throwIfAborted(signal);
    const handler = new (handlerType as new () => THandler)();
    const timer = new ZLinkManagedTimer(
      name,
      periodMs,
      normalizeTimerOptions(options),
      async (tick) => {
        await serial.execute(() => handler.handle(spot, tick));
      }
    );
    this.timers.add(timer);
    return timer;
  }

  async dispose(): Promise<void> {
    const timers = [...this.timers];
    this.timers.clear();
    for (const timer of timers) {
      await timer.dispose();
    }
  }
}

export class ZLinkManagedTimer implements ZLinkTimer {
  private disposed = false;
  private readonly startedAtMs = Date.now();
  private deliveryIndex = 0n;
  private lastScheduledIndex = 0n;
  private timeout: NodeJS.Timeout | undefined;
  private running: Promise<void> = Promise.resolve();

  constructor(
    private readonly name: string,
    private readonly periodMs: number,
    private readonly options: Required<ZLinkTimerOptions>,
    private readonly onTick: (tick: ZLinkTimerTick) => Promise<void>
  ) {
    this.scheduleNext();
  }

  get isDisposed(): boolean {
    return this.disposed;
  }

  async cancel(_signal?: AbortSignal): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    if (this.timeout !== undefined) {
      clearTimeout(this.timeout);
      this.timeout = undefined;
    }
    await this.running;
  }

  dispose(): Promise<void> {
    return this.cancel();
  }

  private scheduleNext(): void {
    if (this.disposed) {
      return;
    }

    const delayMs = this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick
      ? this.periodMs
      : Math.max(0, Number(this.lastScheduledIndex + 1n) * this.periodMs - this.elapsedMs());
    this.timeout = setTimeout(() => {
      this.timeout = undefined;
      this.running = this.fire().catch(() => undefined);
    }, delayMs);
  }

  private async fire(): Promise<void> {
    if (this.disposed) {
      return;
    }

    const scheduledIndex = this.selectScheduledIndex();
    const skippedTicks = scheduledIndex - this.lastScheduledIndex - 1n;
    const startedElapsedMs = this.elapsedMs();
    const scheduledElapsedMs = Number(scheduledIndex) * this.periodMs;
    this.deliveryIndex += 1n;
    const tick: ZLinkTimerTick = {
      name: this.name,
      deliveryIndex: this.deliveryIndex,
      scheduledIndex,
      periodMs: this.periodMs,
      scheduledAt: new Date(this.startedAtMs + scheduledElapsedMs),
      startedAt: new Date(this.startedAtMs + startedElapsedMs),
      scheduledElapsedMs,
      startedElapsedMs,
      delayMs: startedElapsedMs - scheduledElapsedMs,
      skippedTicks
    };

    let shouldContinue = true;
    try {
      await this.onTick(tick);
    } catch {
      shouldContinue = !this.options.stopOnUnhandledException;
    }

    this.lastScheduledIndex = scheduledIndex;
    if (!shouldContinue) {
      this.disposed = true;
      return;
    }
    this.scheduleNext();
  }

  private selectScheduledIndex(): bigint {
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.DelayNextTick) {
      return this.lastScheduledIndex + 1n;
    }

    const dueScheduledIndex = BigInt(Math.max(1, Math.floor(this.elapsedMs() / this.periodMs)));
    if (this.options.overrunPolicy === ZLinkTimerOverrunPolicy.SkipLateTicks) {
      return dueScheduledIndex;
    }

    const availableTicks = dueScheduledIndex - this.lastScheduledIndex;
    const maxCatchUpTicks = BigInt(this.options.maxCatchUpTicks);
    if (availableTicks > maxCatchUpTicks) {
      return dueScheduledIndex - maxCatchUpTicks + 1n;
    }

    return this.lastScheduledIndex + 1n;
  }

  private elapsedMs(): number {
    return Date.now() - this.startedAtMs;
  }
}

export class DefaultZLinkSpotOutbound implements ZLinkSpotOutbound {
  constructor(
    private readonly serial: ZLinkSpotSerialExecutor,
    private readonly channelClient?: ZLinkChannelClient,
    private readonly fanoutClient?: ZLinkFanoutClient,
    private readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver,
    private readonly routedTransport?: ZLinkSpotRoutedTransport
  ) {}

  sendToSpot<TMessage>(spotRid: RoutingId, message: TMessage): ZLinkSendCall {
    return wrapRoutedSpotSendCall(this.serial, this.requireRemoteAddressResolver(), this.requireRoutedTransport(), spotRid, message);
  }

  requestToSpot<TRequest>(spotRid: RoutingId, request: TRequest): ZLinkRequestCall {
    return wrapRoutedSpotRequestCall(
      this.serial,
      this.requireRemoteAddressResolver(),
      this.requireRoutedTransport(),
      spotRid,
      request
    );
  }

  publish<TEvent>(topic: string, event: TEvent): ZLinkPublishCall {
    return wrapPublishCall(this.serial, this.requireFanoutClient().publish(topic, event));
  }

  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall {
    return wrapSendCall(this.serial, this.requireChannelClient().sendToChannel(channelName, message));
  }

  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall {
    return wrapRequestCall(this.serial, this.requireChannelClient().requestToChannel(channelName, request));
  }

  private requireChannelClient(): ZLinkChannelClient {
    if (this.channelClient === undefined) {
      throw new ZLinkConfigurationException('Spot channel outbound runtime is not started.');
    }
    return this.channelClient;
  }

  private requireFanoutClient(): ZLinkFanoutClient {
    if (this.fanoutClient === undefined) {
      throw new ZLinkConfigurationException('Spot publisher runtime is not started.');
    }
    return this.fanoutClient;
  }

  private requireRemoteAddressResolver(): ZLinkSpotRemoteAddressResolver {
    if (this.remoteAddressResolver === undefined) {
      throw new ZLinkConfigurationException(
        'IZLinkSpotOutbound remote address lookup requires a spot remote address resolver.'
      );
    }
    return this.remoteAddressResolver;
  }

  private requireRoutedTransport(): ZLinkSpotRoutedTransport {
    if (this.routedTransport === undefined) {
      throw new ZLinkConfigurationException('Spot routed outbound runtime is not started.');
    }
    return this.routedTransport;
  }
}

export interface ZLinkSpotRoutedTransport {
  sendToSpot<TMessage>(
    remoteAddress: ZLinkSpotRemoteAddress,
    message: TMessage,
    options: ZLinkSpotRoutedSendOptions
  ): Promise<void>;
  requestToSpot<TRequest, TReply = unknown>(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: TRequest,
    options: ZLinkSpotRoutedRequestOptions
  ): Promise<TReply>;
}

export interface ZLinkSpotRoutedSendOptions {
  readonly packetName?: string;
  readonly signal?: AbortSignal;
}

export interface ZLinkSpotRoutedRequestOptions extends ZLinkSpotRoutedSendOptions {
  readonly timeoutMs?: number;
}

export class ZLinkSpotSerialExecutor {
  private tail: Promise<unknown> = Promise.resolve();

  execute<T>(operation: () => Promise<T> | T): Promise<T> {
    const next = this.tail.then(operation, operation);
    this.tail = next.catch(() => undefined);
    return next;
  }
}

function wrapSendCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkSendCall): ZLinkSendCall {
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    submit(signal?: AbortSignal) {
      return serial.execute(() => inner.submit(signal));
    }
  };
}

function wrapPublishCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkPublishCall): ZLinkPublishCall {
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    submit(signal?: AbortSignal) {
      return serial.execute(() => inner.submit(signal));
    }
  };
}

function wrapRequestCall(serial: ZLinkSpotSerialExecutor, inner: ZLinkRequestCall): ZLinkRequestCall {
  return {
    packetName(packetName: string) {
      inner.packetName(packetName);
      return this;
    },
    timeout(timeoutMs: number) {
      inner.timeout(timeoutMs);
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      return serial.execute(() => inner.submit<TReply>(signal));
    }
  };
}

function wrapRoutedSpotSendCall<TMessage>(
  serial: ZLinkSpotSerialExecutor,
  resolver: ZLinkSpotRemoteAddressResolver,
  transport: ZLinkSpotRoutedTransport,
  spotRid: RoutingId,
  message: TMessage
): ZLinkSendCall {
  let selectedPacketName: string | undefined;
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    submit(signal?: AbortSignal) {
      return serial.execute(async () => {
        const remoteAddress = await resolver.resolve(spotRid, signal);
        await transport.sendToSpot(remoteAddress, message, { packetName: selectedPacketName, signal });
      });
    }
  };
}

function wrapRoutedSpotRequestCall<TRequest>(
  serial: ZLinkSpotSerialExecutor,
  resolver: ZLinkSpotRemoteAddressResolver,
  transport: ZLinkSpotRoutedTransport,
  spotRid: RoutingId,
  request: TRequest
): ZLinkRequestCall {
  let selectedPacketName: string | undefined;
  let selectedTimeoutMs: number | undefined;
  return {
    packetName(packetName: string) {
      selectedPacketName = packetName;
      return this;
    },
    timeout(timeoutMs: number) {
      selectedTimeoutMs = timeoutMs;
      return this;
    },
    submit<TReply>(signal?: AbortSignal) {
      return serial.execute(async () => {
        const remoteAddress = await resolver.resolve(spotRid, signal);
        return transport.requestToSpot<TRequest, TReply>(remoteAddress, request, {
          packetName: selectedPacketName,
          timeoutMs: selectedTimeoutMs,
          signal
        });
      });
    }
  };
}

function validateTimer(name: string, periodMs: number, options: ZLinkTimerOptions | undefined): void {
  if (name.trim().length === 0) {
    throw new ZLinkConfigurationException('SPOT timer name must not be empty.');
  }
  if (!Number.isFinite(periodMs) || periodMs <= 0) {
    throw new ZLinkConfigurationException('SPOT timer period must be greater than zero.');
  }
  if (
    options?.overrunPolicy !== undefined
    && !Object.values(ZLinkTimerOverrunPolicy).includes(options.overrunPolicy)
  ) {
    throw new ZLinkConfigurationException('SPOT timer overrun policy is not supported.');
  }
  if (
    options?.overrunPolicy === ZLinkTimerOverrunPolicy.CatchUpBounded
    && (options.maxCatchUpTicks === undefined || options.maxCatchUpTicks <= 0)
  ) {
    throw new ZLinkConfigurationException('SPOT timer MaxCatchUpTicks must be greater than zero.');
  }
}

function normalizeTimerOptions(options: ZLinkTimerOptions | undefined): Required<ZLinkTimerOptions> {
  return {
    overrunPolicy: options?.overrunPolicy ?? ZLinkTimerOverrunPolicy.SkipLateTicks,
    maxCatchUpTicks: options?.maxCatchUpTicks ?? 1,
    stopOnUnhandledException: options?.stopOnUnhandledException ?? false
  };
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}
