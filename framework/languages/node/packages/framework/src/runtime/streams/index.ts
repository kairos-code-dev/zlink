import type {
  ActorRef,
  Message,
  RoutingId,
  Type,
  ZlinkStreamHeader,
  ZLinkActor,
  ZLinkBoundSession,
  ZLinkBoundSessionFactory,
  ZLinkBoundSessionSendCall,
  ZLinkSessionActor,
  ZLinkSessionActors,
  ZLinkSessionClient,
  ZLinkSessionContext,
  ZLinkSessionFactory,
  ZLinkSession,
  ZLinkProviderResolver,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall,
  ZLinkStream
} from '../../contracts';
import { Message as ZLinkBindingMessage } from '@zlink-systems/zlink';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSendFlags,
  ZLinkBackendSpotNode,
  ZLinkBackendStreamSocket
} from '../backend/contracts';
import {
  decodeStreamHeader,
  encodeStreamFrame,
  encodeStreamHeader,
  ensureSingleSubmit,
  lz4Pickle,
  lz4Unpickle,
  messageToBytes,
  requireStreamFrameHeader,
  resolvePacketName,
  tryGetStreamFrameHeader,
  utf8Decode,
  utf8Encode,
  ZLinkStreamCodec,
  type ZLinkStreamFrameHeader,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from './protocol';

export interface ZLinkStreamBindingRuntimeOptions {
  readonly transport?: ZLinkBoundSessionTransport;
  readonly messageFactory?: ZLinkStreamMessageFactory;
  readonly actorBindTimeoutMs?: number;
  readonly actorRefResolver?: (actor: ZLinkActor) => ActorRef;
  readonly relay?: (actor: ZLinkSessionActor, header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal) => Promise<void>;
  readonly notifyDisconnected?: (actor: ZLinkSessionActor, signal?: AbortSignal) => Promise<void>;
}

export interface ZLinkStreamMessageFactory {
  createTextMessage(payload: string): Message;
  createBinaryMessage?(payload: Uint8Array): Message;
}

export interface ZLinkBoundSessionTransport {
  send(actorId: string, message: Message, options: ZLinkBoundSessionSendOptions): Promise<void>;
  disconnect(actorId: string, options: ZLinkBoundSessionDisconnectOptions): Promise<void>;
}

export interface ZLinkBoundSessionSendOptions {
  readonly bindingToken: string;
  readonly packetName?: string;
  readonly metadata: ReadonlyMap<string, string>;
  readonly signal?: AbortSignal;
}

export interface ZLinkBoundSessionDisconnectOptions {
  readonly bindingToken: string;
  readonly signal?: AbortSignal;
}

interface ZLinkActorSessionRoute {
  readonly context: DefaultZLinkSessionContext;
  readonly actor: DefaultZLinkSessionActor;
  readonly bindingToken: string;
}

export interface ZLinkStreamSessionRuntimeOptions {
  readonly socket: ZLinkBackendStreamSocket;
  readonly sessionFactory: (context: DefaultZLinkSessionContext) => ZLinkSession | Promise<ZLinkSession>;
  readonly bindingRuntime?: ZLinkStreamBindingRuntime;
  readonly headerDecoder?: (header: Message) => ZlinkStreamHeader;
  readonly onError?: (error: unknown) => void;
}

export interface ZLinkStreamSessionNodeRuntimeOptions extends ZLinkStreamSessionRuntimeOptions {
  readonly nodeName?: string;
}

export interface ZLinkStreamRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
  readonly bindingRuntime: ZLinkStreamBindingRuntime;
  readonly spotNodes?: ReadonlyMap<string, ZLinkBackendSpotNode>;
  readonly providerResolver?: ZLinkProviderResolver;
}

interface ZLinkStartedStreamNode {
  readonly runtime: ZLinkStreamSessionNodeRuntime;
  readonly socket: ZLinkBackendStreamSocket;
  readonly monitor: ZLinkBackendSocketMonitor;
}

export class ZLinkStreamRuntimeManager {
  private readonly nodes = new Map<string, ZLinkStartedStreamNode>();

  constructor(private readonly options: ZLinkStreamRuntimeManagerOptions) {}

  start(): void {
    if (this.options.registration.streamNodes.size === 0) {
      return;
    }
    const streamAdapter = this.options.backendAdapterFactory.createStreamAdapter();
    const monitoringAdapter = this.options.backendAdapterFactory.createMonitoringAdapter();
    for (const [nodeName, streamNode] of this.options.registration.streamNodes.entries()) {
      const actorGatewayNode = streamNode.attachActorGateway === undefined
        ? undefined
        : this.requireActorGatewayNode(nodeName, streamNode.attachActorGateway);
      const socket = streamAdapter.createStreamSocket(this.options.context);
      if (actorGatewayNode !== undefined) {
        socket.attachActorGateway(actorGatewayNode);
      }
      socket.bind(streamNode.bind!);
      const monitor = monitoringAdapter.openSocketMonitor(socket);
      const sessionType = streamNode.session!;
      const runtime = new ZLinkStreamSessionNodeRuntime({
        nodeName,
        socket,
        bindingRuntime: this.options.bindingRuntime,
        headerDecoder: (header) => decodeStreamHeader(messageToBytes(header)),
        sessionFactory: (context) => createStreamSessionInstance(
          sessionType as Type<ZLinkSession> | Type<ZLinkSessionFactory>,
          this.options.providerResolver,
          context
        )
      });
      runtime.start();
      this.nodes.set(nodeName, { runtime, socket, monitor });
    }
  }

  async dispose(): Promise<void> {
    const nodes = [...this.nodes.values()];
    this.nodes.clear();
    for (const node of nodes.reverse()) {
      await node.runtime.dispose();
      await node.monitor.dispose();
      await node.socket.dispose();
    }
  }

  private requireActorGatewayNode(nodeName: string, spotNodeName: string): ZLinkBackendSpotNode {
    const node = this.options.spotNodes?.get(spotNodeName);
    if (node !== undefined) {
      return node;
    }
    throw new ZLinkConfigurationException(
      `STREAM node '${nodeName}' references unavailable ActorGateway target SpotNode '${spotNodeName}'.`
    );
  }
}

export class ZLinkManagedStream implements ZLinkStream {
  private currentLocalAddr: string | undefined;
  private currentRemoteAddr: string | undefined;

  constructor(
    private readonly socket: ZLinkBackendStreamSocket,
    private readonly backendSessionRoutingId: unknown,
    private readonly publicSessionId = streamSessionIdFromRoutingId(backendSessionRoutingId)
  ) {}

  get sessionId(): string {
    return this.publicSessionId;
  }

  get routingId(): RoutingId {
    return this.publicSessionId;
  }

  get localAddr(): string | undefined {
    return this.currentLocalAddr;
  }

  get remoteAddr(): string | undefined {
    return this.currentRemoteAddr;
  }

  write(payload: Message, flags?: ZLinkBackendSendFlags): boolean {
    return this.socket.send(this.backendRoutingId(), payload, flags ?? 0);
  }

  async close(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    this.socket.disconnectPeer(this.backendRoutingId());
  }

  async bindActor(actor: ActorRef, timeoutMs: number, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await this.socket.bindActor(this.backendRoutingId(), toBackendActorRef(actor), timeoutMs, signal);
  }

  sendBoundActor(actorId: string, parts: readonly Message[], flags?: ZLinkBackendSendFlags): boolean {
    return this.socket.sendBoundActor(this.backendRoutingId(), actorId, parts, flags ?? 0);
  }

  updateAddresses(localAddr: string | undefined, remoteAddr: string | undefined): void {
    this.currentLocalAddr = localAddr;
    this.currentRemoteAddr = remoteAddr;
  }

  private backendRoutingId(): never {
    return this.backendSessionRoutingId as never;
  }
}

export class ZLinkStreamSessionRuntime {
  readonly stream: ZLinkManagedStream;
  readonly context: DefaultZLinkSessionContext;
  private readonly sessionReady: Promise<ZLinkSession>;
  private readonly serial = new ZLinkStreamSessionSerialExecutor();
  private connected = false;
  private disconnected = false;
  private disposed = false;

  constructor(
    private readonly options: ZLinkStreamSessionRuntimeOptions,
    routingId: unknown,
    private readonly removeSession: (sessionId: string) => void = () => {}
  ) {
    const bindingRuntime = options.bindingRuntime ?? new ZLinkStreamBindingRuntime();
    this.stream = new ZLinkManagedStream(options.socket, routingId);
    this.context = bindingRuntime.createSessionContext(this.stream, (signal) => this.close(signal));
    const sessionOrPromise = options.sessionFactory(this.context);
    this.sessionReady = isPromiseLike(sessionOrPromise)
      ? sessionOrPromise.then((session) => this.requireProvidedContext(session))
      : Promise.resolve(this.requireProvidedContext(sessionOrPromise));
  }

  get session(): Promise<ZLinkSession> {
    return this.sessionReady;
  }

  private requireProvidedContext(session: ZLinkSession): ZLinkSession {
    if (session.context !== this.context) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RouteNotConnected,
        'Session must expose the context provided by the stream runtime.'
      );
    }
    return session;
  }

  private async requireSession(): Promise<ZLinkSession> {
    const session = await this.sessionReady;
    return session;
  }

  enqueueConnected(localAddr?: string, remoteAddr?: string): void {
    this.enqueue(async () => this.markConnected(localAddr, remoteAddr));
  }

  enqueuePacket(header: Message, payload: Message): void {
    this.enqueue(
      async () => this.dispatchPacket(header, payload),
      () => {
        header.close();
        payload.close();
      }
    );
  }

  enqueueDisconnected(error?: unknown): void {
    if (this.disconnected) {
      return;
    }
    this.disconnected = true;
    this.enqueue(async () => this.complete(error, true));
  }

  async close(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await this.stream.close(signal);
    if (this.disconnected) {
      return;
    }
    this.disconnected = true;
    this.enqueue(async () => this.complete(undefined, true));
  }

  async dispose(): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    await this.serial.dispose();
    if (!this.disconnected) {
      this.disconnected = true;
      const session = await this.requireSession();
      await session.onDisconnected?.(this.context);
    }
    await this.cleanup();
  }

  private async markConnected(localAddr?: string, remoteAddr?: string): Promise<void> {
    this.stream.updateAddresses(localAddr, remoteAddr);
    if (this.connected) {
      return;
    }
    this.connected = true;
    const session = await this.requireSession();
    await session.onConnected?.(this.context);
  }

  private async dispatchPacket(header: Message, payload: Message): Promise<void> {
    let decodedHeader: ZlinkStreamHeader | undefined;
    let dispatchPayload = payload;
    try {
      decodedHeader = this.options.headerDecoder?.(header) ?? header;
      dispatchPayload = this.context.payloadForHeader(decodedHeader, payload);
      if (this.context.tryCompleteResponse(decodedHeader, dispatchPayload)) {
        return;
      }
      this.context.enterDispatch(decodedHeader);
      const session = await this.requireSession();
      await session.onDispatch?.(decodedHeader, dispatchPayload);
    } catch (error) {
      this.options.onError?.(error);
      await this.replyDispatchError(decodedHeader, error);
    } finally {
      if (decodedHeader !== undefined) {
        this.context.exitDispatch();
      }
      header.close();
      if (dispatchPayload !== payload) {
        dispatchPayload.close();
      }
      payload.close();
    }
  }

  private async replyDispatchError(header: ZlinkStreamHeader | undefined, error: unknown): Promise<void> {
    const decoded = tryGetStreamFrameHeader(header);
    if (decoded?.requestSeq === undefined) {
      return;
    }
    const frame = encodeStreamFrame(
      {
        kind: ZLinkStreamMessageKind.Error,
        codec: ZLinkStreamCodec.Json,
        flags: ZLinkStreamHeaderFlags.HasRequestSeq,
        requestSeq: decoded.requestSeq,
        name: decoded.name,
        metadata: new Map()
      },
      utf8Encode(JSON.stringify({
        code: error instanceof Error ? error.constructor.name : undefined,
        message: error instanceof Error ? error.message : String(error)
      }))
    );
    const message = simpleMessage(frame) as Message;
    try {
      if (!this.context.stream.write(message)) {
        throw new Error('Client stream error reply send failed.');
      }
    } catch (replyError) {
      this.options.onError?.(replyError);
    } finally {
      message.close();
    }
  }

  private async complete(error: unknown, notifyDisconnected: boolean): Promise<void> {
    if (error !== undefined) {
      const session = await this.requireSession();
      await session.onError?.(this.context, {
        error: 'transportError' as never,
        diagnostic: {
          message: error instanceof Error ? error.message : String(error)
        }
      });
    }
    if (notifyDisconnected) {
      const session = await this.requireSession();
      await session.onDisconnected?.(this.context);
    }
    await this.cleanup();
  }

  private async cleanup(): Promise<void> {
    this.context.cleanupBindings();
    this.removeSession(this.stream.sessionId);
  }

  private enqueue(work: () => Promise<void>, onRejected?: () => void): void {
    if (!this.serial.enqueue(work)) {
      onRejected?.();
    }
  }
}

export class ZLinkStreamSessionNodeRuntime {
  private readonly sessions = new Map<string, ZLinkStreamSessionRuntime>();
  private stopped = false;

  constructor(private readonly options: ZLinkStreamSessionNodeRuntimeOptions) {}

  start(): void {
    this.options.socket.onFramedPacket((routingId, header, payload) => {
      this.onFramedPacket(routingId, header, payload);
    });
  }

  markConnected(routingId: unknown, localAddr?: string, remoteAddr?: string): void {
    this.getOrCreateSession(routingId).enqueueConnected(localAddr, remoteAddr);
  }

  markDisconnected(routingId: unknown, error?: unknown): void {
    this.sessions.get(streamSessionIdFromRoutingId(routingId))?.enqueueDisconnected(error);
  }

  findSession(routingId: unknown): ZLinkStreamSessionRuntime | undefined {
    return this.sessions.get(streamSessionIdFromRoutingId(routingId));
  }

  async dispose(): Promise<void> {
    this.stopped = true;
    const sessions = [...this.sessions.values()];
    this.sessions.clear();
    for (const session of sessions) {
      await session.dispose();
    }
  }

  private onFramedPacket(routingId: unknown, header: Message, payload: Message): void {
    if (this.stopped) {
      header.close();
      payload.close();
      return;
    }
    this.getOrCreateSession(routingId).enqueuePacket(header, payload);
  }

  private getOrCreateSession(routingId: unknown): ZLinkStreamSessionRuntime {
    const sessionId = streamSessionIdFromRoutingId(routingId);
    const existing = this.sessions.get(sessionId);
    if (existing !== undefined) {
      return existing;
    }
    const created = new ZLinkStreamSessionRuntime(
      this.options,
      routingId,
      (sessionId) => this.sessions.delete(sessionId)
    );
    this.sessions.set(sessionId, created);
    return created;
  }
}

class ZLinkStreamSessionSerialExecutor {
  private tail: Promise<void> | undefined;
  private closed = false;

  enqueue(work: () => Promise<void>): boolean {
    if (this.closed) {
      return false;
    }
    const previous = this.tail;
    const next = (previous === undefined ? runNow(work) : previous.then(work, work))
      .catch(() => {});
    this.tail = next;
    next.finally(() => {
      if (this.tail === next) {
        this.tail = undefined;
      }
    });
    return true;
  }

  async dispose(): Promise<void> {
    this.closed = true;
    await this.tail;
  }
}

async function runNow(work: () => Promise<void>): Promise<void> {
  await Promise.resolve();
  await work();
}

export class ZLinkStreamBindingRuntime {
  private readonly routes: ZLinkActorSessionBindingRegistry;
  private readonly frameMessages: ZLinkStreamFrameMessageFactory;

  constructor(private readonly options: ZLinkStreamBindingRuntimeOptions = {}) {
    this.routes = new ZLinkActorSessionBindingRegistry();
    this.frameMessages = new ZLinkStreamFrameMessageFactory(options);
  }

  createSessionContext(stream: ZLinkStream, close?: (signal?: AbortSignal) => Promise<void>): DefaultZLinkSessionContext {
    return new DefaultZLinkSessionContext(this, stream, close ?? ((signal) => stream.close(signal)));
  }

  createBoundSession(actorId: string): ZLinkBoundSession {
    return new DefaultZLinkBoundSession(this, actorId);
  }

  async bind(
    context: DefaultZLinkSessionContext,
    actorOrRef: ZLinkActor | ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    throwIfAborted(signal);
    const actorRef = isActorRef(actorOrRef)
      ? actorOrRef
      : this.resolveActorRef(actorOrRef);
    if (actorRef.actorId.trim().length === 0) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor id must not be empty.'
      );
    }
    if (context.routingId === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor session binding requires a stream routing id.'
      );
    }

    await this.bindNativeActor(context, actorRef, signal);

    const bindingToken = createBindingToken();
    const sessionActor = new DefaultZLinkSessionActor(this, actorRef, bindingToken);
    this.routes.bind(context, sessionActor, bindingToken);
    return sessionActor;
  }

  find(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.routes.find(actorId);
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    this.routes.unbind(actorId, context, bindingToken);
  }

  cleanup(context: DefaultZLinkSessionContext): void {
    this.routes.cleanup(context);
  }

  async sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.require(actorId);
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message
    );
    try {
      await this.requireTransport().send(actorId, frame, {
        bindingToken: route.bindingToken,
        packetName,
        metadata,
        signal
      });
      this.routes.requireCurrentToken(actorId, route.bindingToken);
    } finally {
      frame.close();
    }
  }

  async disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.require(actorId);
    try {
      await this.requireTransport().disconnect(actorId, {
        bindingToken: route.bindingToken,
        signal
      });
    } finally {
      this.unbind(actorId, route.context, route.bindingToken);
    }
  }

  async relay(actor: DefaultZLinkSessionActor, header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void> {
    this.routes.requireCurrentToken(actor.actorId, actor.bindingToken);
    if (this.options.relay !== undefined) {
      await this.options.relay(actor, header, payload, signal);
      return;
    }
    const route = this.routes.require(actor.actorId);
    if (!(route.context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    const headerMessage = this.frameMessages.createBinaryMessage(encodeStreamHeader(requireStreamFrameHeader(header)));
    const payloadMessage = this.frameMessages.createBinaryMessage(messageToBytes(payload));
    try {
      if (!route.context.stream.sendBoundActor(actor.actorId, [headerMessage, payloadMessage], 0)) {
        throw new Error('Actor session relay failed because the ActorGateway route was not ready before timeout.');
      }
    } finally {
      headerMessage.close();
      payloadMessage.close();
    }
  }

  async notifyDisconnected(actor: DefaultZLinkSessionActor, signal?: AbortSignal): Promise<void> {
    this.routes.requireCurrentToken(actor.actorId, actor.bindingToken);
    await this.options.notifyDisconnected?.(actor, signal);
  }

  createTextMessage(payload: string): Message {
    return this.frameMessages.createTextMessage(payload);
  }

  createBinaryMessage(payload: Uint8Array): Message {
    return this.frameMessages.createBinaryMessage(payload);
  }

  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown
  ): Message {
    return this.frameMessages.createJsonFrameMessage(kind, packetName, metadata, compressed, requestSeq, payload);
  }

  private resolveActorRef(actor: ZLinkActor): ActorRef {
    if (this.options.actorRefResolver !== undefined) {
      return this.options.actorRefResolver(actor);
    }
    const state = actor.context as unknown as { actorRef?: ActorRef };
    if (state.actorRef !== undefined) {
      return state.actorRef;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `Actor '${actor.actorId}' does not have a concrete actor ref.`
    );
  }

  private async bindNativeActor(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    await context.stream.bindActor(actorRef, this.options.actorBindTimeoutMs ?? 2000, signal);
  }

  private requireTransport(): ZLinkBoundSessionTransport {
    if (this.options.transport === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorSessionNotBound,
        'Bound session transport is not started.',
        true
      );
    }
    return this.options.transport;
  }
}

class ZLinkActorSessionBindingRegistry {
  private readonly routes = new Map<string, ZLinkActorSessionRoute>();

  bind(context: DefaultZLinkSessionContext, actor: DefaultZLinkSessionActor, bindingToken: string): void {
    this.routes.set(actor.actorId, { context, actor, bindingToken });
    context.bindLocal(actor, bindingToken);
  }

  find(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.routes.get(actorId)?.actor;
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined || route.context !== context || route.bindingToken !== bindingToken) {
      return;
    }
    this.routes.delete(actorId);
    context.unbindLocal(actorId, bindingToken);
  }

  cleanup(context: DefaultZLinkSessionContext): void {
    for (const route of [...this.routes.values()]) {
      if (route.context === context) {
        this.unbind(route.actor.actorId, context, route.bindingToken);
      }
    }
  }

  require(actorId: string): ZLinkActorSessionRoute {
    const route = this.routes.get(actorId);
    if (route !== undefined) {
      return route;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `No current session binding exists for actor '${actorId}'.`,
      true
    );
  }

  requireCurrentToken(actorId: string, bindingToken: string): void {
    const route = this.require(actorId);
    if (route.bindingToken === bindingToken) {
      return;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `Actor '${actorId}' session binding is stale.`,
      true
    );
  }
}

class ZLinkStreamFrameMessageFactory {
  constructor(private readonly options: ZLinkStreamBindingRuntimeOptions) {}

  createTextMessage(payload: string): Message {
    return this.requireMessageFactory().createTextMessage(payload);
  }

  createBinaryMessage(payload: Uint8Array): Message {
    const factory = this.requireMessageFactory();
    if (factory.createBinaryMessage !== undefined) {
      return factory.createBinaryMessage(payload);
    }
    return factory.createTextMessage(utf8Decode(payload));
  }

  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown
  ): Message {
    let body = utf8Encode(JSON.stringify(payload));
    if (compressed) {
      body = lz4Pickle(body);
    }
    const flags = compressed ? ZLinkStreamHeaderFlags.PayloadCompressed : ZLinkStreamHeaderFlags.None;
    const frame = encodeStreamFrame(
      {
        kind,
        codec: ZLinkStreamCodec.Json,
        flags,
        requestSeq,
        name: packetName,
        metadata
      },
      body
    );
    return this.createBinaryMessage(frame);
  }

  private requireMessageFactory(): ZLinkStreamMessageFactory {
    return this.options.messageFactory ?? defaultStreamMessageFactory;
  }
}

const defaultStreamMessageFactory: ZLinkStreamMessageFactory = {
  createTextMessage(payload: string): Message {
    return ZLinkBindingMessage.from(Buffer.from(payload));
  },
  createBinaryMessage(payload: Uint8Array): Message {
    return ZLinkBindingMessage.from(Buffer.from(payload));
  }
};

export class DefaultZLinkSessionContext implements ZLinkSessionContext {
  readonly client: ZLinkSessionClient;
  readonly actors: ZLinkSessionActors;
  private readonly localActors = new ZLinkSessionLocalActorBindings();
  private readonly requests = new ZLinkSessionRequestTracker();
  private currentDispatchHeader: ZLinkStreamFrameHeader | undefined;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    readonly stream: ZLinkStream,
    private readonly closeSession: (signal?: AbortSignal) => Promise<void>
  ) {
    this.client = new DefaultZLinkSessionClient(this);
    this.actors = new DefaultZLinkSessionActors(this, runtime);
  }

  get sessionId(): string {
    return this.stream.sessionId;
  }

  get routingId(): RoutingId | undefined {
    return this.stream.routingId;
  }

  get localAddr(): string | undefined {
    return this.stream.localAddr;
  }

  get remoteAddr(): string | undefined {
    return this.stream.remoteAddr;
  }

  async close(signal?: AbortSignal): Promise<void> {
    this.runtime.cleanup(this);
    await this.closeSession(signal);
  }

  cleanupBindings(): void {
    this.runtime.cleanup(this);
  }

  createTextMessage(payload: string): Message {
    return this.runtime.createTextMessage(payload);
  }

  createBinaryMessage(payload: Uint8Array): Message {
    return this.runtime.createBinaryMessage(payload);
  }

  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown
  ): Message {
    return this.runtime.createJsonFrameMessage(
      kind,
      packetName,
      metadata,
      compressed,
      requestSeq,
      payload
    );
  }

  enterDispatch(header: ZlinkStreamHeader): void {
    this.currentDispatchHeader = tryGetStreamFrameHeader(header);
  }

  exitDispatch(): void {
    this.currentDispatchHeader = undefined;
  }

  get dispatchHeader(): ZLinkStreamFrameHeader | undefined {
    return this.currentDispatchHeader;
  }

  startRequest(timeoutMs?: number): ZLinkPendingSessionRequest {
    return this.requests.start(timeoutMs);
  }

  tryCompleteResponse(header: ZlinkStreamHeader, payload: Message): boolean {
    const decoded = tryGetStreamFrameHeader(header);
    if (decoded?.kind !== ZLinkStreamMessageKind.Response || decoded.requestSeq === undefined) {
      return false;
    }
    return this.requests.complete(decoded.requestSeq, payload);
  }

  payloadForHeader(header: ZlinkStreamHeader, payload: Message): Message {
    const decoded = tryGetStreamFrameHeader(header);
    if (decoded === undefined || (decoded.flags & ZLinkStreamHeaderFlags.PayloadCompressed) === 0) {
      return payload;
    }
    return simpleMessage(lz4Unpickle(messageToBytes(payload))) as Message;
  }

  get boundActors(): readonly DefaultZLinkSessionActor[] {
    return this.localActors.snapshot();
  }

  findBoundActor(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.localActors.find(actorId);
  }

  bindLocal(actor: DefaultZLinkSessionActor, token: string): void {
    this.localActors.bind(actor, token);
  }

  unbindLocal(actorId: string, token: string): void {
    this.localActors.unbind(actorId, token);
  }
}

class ZLinkSessionLocalActorBindings {
  private readonly actors = new Map<string, { actor: DefaultZLinkSessionActor; token: string }>();

  snapshot(): readonly DefaultZLinkSessionActor[] {
    const snapshot: DefaultZLinkSessionActor[] = [];
    for (const entry of this.actors.values()) {
      snapshot.push(entry.actor);
    }
    return snapshot;
  }

  find(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.actors.get(actorId)?.actor;
  }

  bind(actor: DefaultZLinkSessionActor, token: string): void {
    this.actors.set(actor.actorId, { actor, token });
  }

  unbind(actorId: string, token: string): void {
    const current = this.actors.get(actorId);
    if (current?.token === token) {
      this.actors.delete(actorId);
    }
  }
}

class DefaultZLinkSessionClient implements ZLinkSessionClient {
  constructor(private readonly context: DefaultZLinkSessionContext) {}

  send<TMessage>(message: TMessage): ZLinkSessionSendCall {
    return new DefaultZLinkSessionSendCall(this.context, message);
  }

  reply<TMessage>(message: TMessage): ZLinkSessionReplyCall {
    return new DefaultZLinkSessionReplyCall(this.context, message);
  }
}

class DefaultZLinkSessionActors implements ZLinkSessionActors {
  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly runtime: ZLinkStreamBindingRuntime
  ) {}

  get bound(): readonly ZLinkSessionActor[] {
    return this.context.boundActors;
  }

  async bind(actor: ZLinkActor | ActorRef, signal?: AbortSignal): Promise<ZLinkSessionActor> {
    throwIfAborted(signal);
    return await this.runtime.bind(this.context, actor, signal);
  }

  find(actorId: string): ZLinkSessionActor | undefined {
    return this.context.findBoundActor(actorId);
  }
}

export class DefaultZLinkSessionActor implements ZLinkSessionActor {
  readonly actorId: string;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    readonly ref: ActorRef,
    readonly bindingToken: string
  ) {
    this.actorId = ref.actorId;
  }

  relay(header: ZlinkStreamHeader, payload: Message, signal?: AbortSignal): Promise<void> {
    return this.runtime.relay(this, header, payload, signal);
  }

  notifyDisconnected(signal?: AbortSignal): Promise<void> {
    return this.runtime.notifyDisconnected(this, signal);
  }
}

export class DefaultZLinkBoundSession implements ZLinkBoundSession {
  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly actorId: string
  ) {}

  send<TMessage>(message: TMessage): ZLinkBoundSessionSendCall {
    return new DefaultZLinkBoundSessionSendCall(this.runtime, this.actorId, message);
  }

  disconnect(signal?: AbortSignal): Promise<void> {
    return this.runtime.disconnectBoundSession(this.actorId, signal);
  }
}

export class DefaultZLinkBoundSessionFactory implements ZLinkBoundSessionFactory {
  constructor(private readonly runtime: ZLinkStreamBindingRuntime) {}

  create(actorId: string): ZLinkBoundSession {
    return new DefaultZLinkBoundSession(this.runtime, actorId);
  }
}

export class ZLinkPendingSessionRequest {
  private completed = false;
  private readonly timeout: ReturnType<typeof setTimeout> | undefined;
  private resolvePromise!: (message: Message) => void;
  private rejectPromise!: (error: unknown) => void;
  readonly promise: Promise<Message>;

  constructor(
    private readonly tracker: ZLinkSessionRequestTracker,
    readonly requestSeq: bigint,
    timeoutMs: number | undefined
  ) {
    this.promise = new Promise<Message>((resolve, reject) => {
      this.resolvePromise = resolve;
      this.rejectPromise = reject;
    });
    if (timeoutMs !== undefined) {
      this.timeout = setTimeout(() => this.cancel(), timeoutMs);
    }
  }

  complete(payload: Message): void {
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.clearTimeout();
    this.resolvePromise(copyMessage(payload));
  }

  cancel(): void {
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.clearTimeout();
    this.tracker.remove(this.requestSeq);
    this.rejectPromise(new Error('Client stream request timed out.'));
  }

  dispose(): void {
    this.clearTimeout();
    this.tracker.remove(this.requestSeq);
  }

  private clearTimeout(): void {
    if (this.timeout !== undefined) {
      clearTimeout(this.timeout);
    }
  }
}

class ZLinkSessionRequestTracker {
  private readonly pending = new Map<bigint, ZLinkPendingSessionRequest>();
  private nextRequestSeq = 0n;

  start(timeoutMs?: number): ZLinkPendingSessionRequest {
    const requestSeq = this.next();
    const pending = new ZLinkPendingSessionRequest(this, requestSeq, timeoutMs);
    if (this.pending.has(requestSeq)) {
      throw new Error('Duplicate stream request sequence.');
    }
    this.pending.set(requestSeq, pending);
    return pending;
  }

  complete(requestSeq: bigint, payload: Message): boolean {
    const pending = this.pending.get(requestSeq);
    if (pending === undefined) {
      return false;
    }
    this.pending.delete(requestSeq);
    pending.complete(payload);
    return true;
  }

  remove(requestSeq: bigint): void {
    this.pending.delete(requestSeq);
  }

  private next(): bigint {
    do {
      this.nextRequestSeq = (this.nextRequestSeq + 1n) & 0xffffffffffffffffn;
    } while (this.nextRequestSeq === 0n);
    return this.nextRequestSeq;
  }
}

class DefaultZLinkBoundSessionSendCall<TMessage> implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly actorId: string,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    ensureSingleSubmit(this.executed);
    this.executed = true;
    return this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
  }
}

class DefaultZLinkSessionSendCall<TMessage> implements ZLinkSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  packetName(packetName: string): this {
    this.selectedPacketName = packetName;
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    ensureSingleSubmit(this.executed);
    this.executed = true;
    const message = this.context.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(this.message, this.selectedPacketName),
      this.selectedMetadata,
      this.compressionEnabled,
      undefined,
      this.message
    );
    try {
      if (!this.context.stream.write(message)) {
        throw new Error('Client stream send failed.');
      }
    } finally {
      message.close();
    }
  }
}

class DefaultZLinkSessionReplyCall<TMessage> implements ZLinkSessionReplyCall {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: TMessage
  ) {}

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    ensureSingleSubmit(this.executed);
    this.executed = true;
    const requestHeader = this.context.dispatchHeader;
    if (requestHeader?.requestSeq === undefined) {
      throw new Error('Reply is only available while handling a request packet.');
    }
    const message = this.context.createJsonFrameMessage(
      ZLinkStreamMessageKind.Response,
      requestHeader.name,
      this.selectedMetadata,
      this.compressionEnabled,
      requestHeader.requestSeq,
      this.message
    );
    try {
      if (!this.context.stream.write(message)) {
        throw new Error('Client stream reply send failed.');
      }
    } finally {
      message.close();
    }
  }
}

function isActorRef(value: ZLinkActor | ActorRef): value is ActorRef {
  return (
    typeof value === 'object'
    && value !== null
    && 'nodeRid' in value
    && 'generation' in value
    && 'actorId' in value
  );
}

function createBindingToken(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

function toBackendActorRef(actor: ActorRef): ZLinkBackendActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: actor.generation
  };
}

function copyMessage(message: Message): Message {
  const value = message as unknown as {
    copy?: () => Message;
    toBytes?: () => Uint8Array;
    data?: () => Uint8Array;
    bytes?: Uint8Array;
    getString?: () => string;
  };
  if (value.copy !== undefined) {
    return value.copy();
  }
  if (value.toBytes !== undefined) {
    return simpleMessage(value.toBytes()) as Message;
  }
  if (value.data !== undefined) {
    return simpleMessage(value.data()) as Message;
  }
  if (value.bytes !== undefined) {
    return simpleMessage(value.bytes) as Message;
  }
  if (value.getString !== undefined) {
    return simpleMessage(utf8Encode(value.getString())) as Message;
  }
  throw new Error('Stream response payload cannot be copied.');
}

function simpleMessage(bytes: Uint8Array): unknown {
  const copy = new Uint8Array(bytes);
  return {
    bytes: copy,
    toBytes() {
      return new Uint8Array(copy);
    },
    data() {
      return copy;
    },
    getString() {
      return utf8Decode(copy);
    },
    close() {}
  };
}

function streamSessionIdFromRoutingId(routingId: unknown): string {
  if (typeof routingId === 'string') {
    return routingId;
  }
  if (
    typeof routingId === 'object'
    && routingId !== null
    && 'toHex' in routingId
    && typeof (routingId as { toHex?: unknown }).toHex === 'function'
  ) {
    return (routingId as { toHex(): string }).toHex();
  }
  if (
    typeof routingId === 'object'
    && routingId !== null
    && 'toString' in routingId
    && typeof (routingId as { toString?: unknown }).toString === 'function'
  ) {
    return (routingId as { toString(): string }).toString();
  }
  throw new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    'Stream session routing id is invalid.'
  );
}

function isPromiseLike<T>(value: T | Promise<T>): value is Promise<T> {
  return typeof (value as { then?: unknown }).then === 'function';
}

async function createProviderInstance<T>(
  type: Type<T>,
  resolver: ZLinkProviderResolver | undefined,
  fallbackArg?: unknown
): Promise<T> {
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  return fallbackArg === undefined
    ? new (type as new () => T)()
    : new (type as new (arg: unknown) => T)(fallbackArg);
}

async function createStreamSessionInstance(
  type: Type<ZLinkSession> | Type<ZLinkSessionFactory>,
  resolver: ZLinkProviderResolver | undefined,
  context: DefaultZLinkSessionContext
): Promise<ZLinkSession> {
  const created = await createProviderInstance<ZLinkSession | ZLinkSessionFactory>(
    type as Type<ZLinkSession | ZLinkSessionFactory>,
    resolver,
    context
  );
  if (isSessionFactory(created)) {
    return await created.create(context);
  }
  return created as ZLinkSession;
}

function isSessionFactory(value: unknown): value is ZLinkSessionFactory {
  return typeof (value as { create?: unknown }).create === 'function'
    && (value as { context?: unknown }).context === undefined;
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}
