import type {
  ActorRef,
  RoutingId,
  Type,
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
  ZLinkSessionDispatchContext,
  ZLinkProviderResolver,
  ZLinkSessionReplyCall,
  ZLinkSessionSendCall,
  ZLinkStream,
  ZLinkMessageSerializer,
  ZLinkStreamCompressionCodec,
  ZLinkStreamCompressionOptions
} from '../../contracts';
import { Message as ZLinkBindingMessage, SubmitError, SubmitResult } from '@zlink-systems/zlink';
import {
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessage,
  ZLinkMessageFlowPhase
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkDispatchErrorReporter } from '../channels';
import { flowIfEnabled } from '../diagnostics';
import {
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
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
  resolvePacketName,
  utf8Decode,
  utf8Encode,
  ZLinkStreamCodec,
  type ZLinkStreamFrameHeader,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from './protocol';

const ZLINK_SEND_DONT_WAIT = 1 as ZLinkBackendSendFlags;
const ZLINK_NATIVE_BOUND_SESSION_RETRY_DELAY_MS = 10;
const REMOTE_BOUND_SESSION_BIND_PACKET = 'zlink.framework.actor.bound_session.bind';
const DEFAULT_MAX_DECOMPRESSED_STREAM_PAYLOAD_SIZE = 64 * 1024;

function createDispatchContext(header: ZLinkStreamFrameHeader): ZLinkSessionDispatchContext {
  return {
    packetName: header.name,
    metadata: header.metadata,
    canReply: header.requestSeq !== undefined
  };
}

export interface ZLinkStreamBindingRuntimeOptions {
  readonly transport?: ZLinkBoundSessionTransport;
  readonly messageFactory?: ZLinkStreamMessageFactory;
  readonly streamPayloadCodec?: ZLinkStreamPayloadCodec;
  readonly streamCompression?: ZLinkStreamCompressionOptions;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly actorBindTimeoutMs?: number;
  readonly actorRefResolver?: (actor: ZLinkActor) => ActorRef;
  readonly nativeActorNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly relay?: (actor: ZLinkSessionActor, header: ZLinkStreamFrameHeader, payload: Message, signal?: AbortSignal) => Promise<boolean>;
  readonly notifyDisconnected?: (actor: ZLinkSessionActor, signal?: AbortSignal) => Promise<void>;
}

export interface ZLinkStreamPayloadCodec {
  encode(payload: unknown): {
    readonly codec: ZLinkStreamCodec;
    readonly payload: Uint8Array;
  };
}

export interface ZLinkStreamMessageFactory {
  createTextMessage(payload: string): Message;
  createBinaryMessage?(payload: Uint8Array): Message;
}

export interface ZLinkBoundSessionTransport {
  send(actorId: string, message: unknown, options: ZLinkBoundSessionSendOptions): Promise<void>;
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
  readonly onError?: (error: unknown) => void;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
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
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
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
      const socket = streamAdapter.createStreamSocket(this.options.context);
      socket.bind(streamNode.bind!);
      const monitor = monitoringAdapter.openSocketMonitor(socket);
      const sessionType = streamNode.session!;
      const runtime = new ZLinkStreamSessionNodeRuntime({
        nodeName,
        socket,
        bindingRuntime: this.options.bindingRuntime,
        dispatchErrors: this.options.dispatchErrors,
        messageSerializers: this.options.registration.messageSerializers,
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

}

export class ZLinkManagedStream implements ZLinkStream {
  private currentLocalAddr: string | undefined;
  private currentRemoteAddr: string | undefined;

  constructor(
    private readonly socket: ZLinkBackendStreamSocket,
    private readonly backendSessionRoutingId: unknown,
    private readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>,
    private readonly publicSessionId = streamSessionIdFromRoutingId(backendSessionRoutingId)
  ) {}

  get sessionId(): string {
    return this.publicSessionId;
  }

  get routingId(): RoutingId {
    return this.publicSessionId;
  }

  get actorBindingRoutingId(): RoutingId {
    return this.backendSessionRoutingId as RoutingId;
  }

  get localAddr(): string | undefined {
    return this.currentLocalAddr;
  }

  get remoteAddr(): string | undefined {
    return this.currentRemoteAddr;
  }

  write(payload: ZLinkMessage, flags?: ZLinkBackendSendFlags): boolean {
    return this.socket.send(
      this.backendRoutingId(),
      encodeFrameworkPayloadMessage(payload, this.messageSerializers),
      flags ?? 0
    );
  }

  writeRaw(payload: Message, flags?: ZLinkBackendSendFlags): boolean {
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
    private readonly routingId: unknown,
    private readonly removeSession: (sessionId: string) => void = () => {}
  ) {
    const bindingRuntime = options.bindingRuntime ?? new ZLinkStreamBindingRuntime();
    this.stream = new ZLinkManagedStream(options.socket, routingId, options.messageSerializers);
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
    let decodedHeader: ZLinkStreamFrameHeader | undefined;
    let dispatchPayload = payload;
    try {
      decodedHeader = decodeStreamHeader(messageToBytes(header));
      dispatchPayload = this.context.payloadForHeader(decodedHeader, payload);
      if (this.context.tryCompleteResponse(decodedHeader, dispatchPayload)) {
        return;
      }
      this.context.enterDispatch(decodedHeader);
      const session = await this.requireSession();
      const streamKind = decodedHeader.kind === ZLinkStreamMessageKind.Request
        ? ZLinkDispatchMessageKind.Request
        : ZLinkDispatchMessageKind.Send;
      const streamCorr = decodedHeader.correlationId ?? decodedHeader.requestSeq?.toString();
      flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowPhase.Received)?.trace({
        phase: ZLinkMessageFlowPhase.Received,
        surface: ZLinkDispatchErrorSurface.StreamSession,
        messageKind: streamKind,
        packetName: decodedHeader.name,
        correlationId: streamCorr,
        sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId)
      });
      await session.onDispatch?.(
        createDispatchContext(decodedHeader),
        wrapFrameworkPayloadMessage(dispatchPayload, this.options.messageSerializers)
      );
      flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowPhase.Dispatched)?.trace({
        phase: ZLinkMessageFlowPhase.Dispatched,
        surface: ZLinkDispatchErrorSurface.StreamSession,
        messageKind: streamKind,
        packetName: decodedHeader.name,
        correlationId: streamCorr,
        sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId)
      });
    } catch (error) {
      this.options.dispatchErrors?.report({
        surface: ZLinkDispatchErrorSurface.StreamSession,
        messageKind: decodedHeader?.kind === ZLinkStreamMessageKind.Request
          ? ZLinkDispatchMessageKind.Request
          : ZLinkDispatchMessageKind.Send,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action: decodedHeader?.requestSeq === undefined
          ? ZLinkDispatchErrorAction.Drop
          : ZLinkDispatchErrorAction.ReplyError,
        packetName: decodedHeader?.name,
        sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId),
        correlationId: decodedHeader?.correlationId ?? decodedHeader?.requestSeq?.toString(),
        error
      });
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

  private async replyDispatchError(header: ZLinkStreamFrameHeader | undefined, error: unknown): Promise<void> {
    if (header?.requestSeq === undefined) {
      return;
    }
    const frame = encodeStreamFrame(
      {
        kind: ZLinkStreamMessageKind.Error,
        codec: ZLinkStreamCodec.Json,
        flags: ZLinkStreamHeaderFlags.HasRequestSeq,
        requestSeq: header.requestSeq,
        name: header.name,
        metadata: new Map(),
        correlationId: header.correlationId
      },
      utf8Encode(JSON.stringify({
        code: error instanceof Error ? error.constructor.name : undefined,
        message: error instanceof Error ? error.message : String(error)
      }))
    );
    const message = simpleMessage(frame) as Message;
    try {
      if (!this.context.stream.writeRaw(message)) {
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
  private readonly compressionCodec: ZLinkStreamCompressionCodec | undefined;

  constructor(private readonly options: ZLinkStreamBindingRuntimeOptions = {}) {
    this.routes = new ZLinkActorSessionBindingRegistry();
    this.compressionCodec = resolveStreamCompressionCodec(options.streamCompression);
    this.frameMessages = new ZLinkStreamFrameMessageFactory(options);
  }

  createSessionContext(stream: ZLinkManagedStream, close?: (signal?: AbortSignal) => Promise<void>): DefaultZLinkSessionContext {
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

  hasBoundSession(actorId: string): boolean {
    return this.routes.find(actorId) !== undefined;
  }

  async rebindActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.route(actorRef.actorId);
    if (route === undefined) {
      return;
    }
    if (sameActorRef(route.actor.ref, actorRef)) {
      return;
    }
    await this.bindNativeActor(route.context, actorRef, signal);
    this.relayRemoteBoundSessionBind(route.context, actorRef);
  }

  async refreshActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.route(actorRef.actorId);
    if (route === undefined) {
      return;
    }
    await this.bindNativeActor(route.context, actorRef, signal);
    this.relayRemoteBoundSessionBind(route.context, actorRef);
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    this.routes.unbind(actorId, context, bindingToken);
  }

  unbindActor(actorId: string): void {
    this.routes.unbindActor(actorId);
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
    const route = this.routes.requireRoute(actorId);
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

  sendLocalBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message
    );
    try {
      if (!route.context.stream.writeRaw(frame)) {
        throw new Error(`Actor '${actorId}' local bound session send failed.`);
      }
      this.routes.requireCurrentToken(actorId, route.bindingToken);
      return true;
    } finally {
      frame.close();
    }
  }

  sendLocalBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Response,
      packetName,
      metadata,
      false,
      requestSeq,
      message
    );
    try {
      if (!route.context.stream.writeRaw(frame)) {
        throw new Error(`Actor '${actorId}' local bound session response failed.`);
      }
      this.routes.requireCurrentToken(actorId, route.bindingToken);
      return true;
    } finally {
      frame.close();
    }
  }

  sendLocalBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    const route = this.routes.route(actorId);
    if (route === undefined) {
      return false;
    }
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Error,
      packetName,
      metadata,
      false,
      requestSeq,
      {
        code: error instanceof Error ? error.constructor.name : undefined,
        message: error instanceof Error ? error.message : String(error)
      }
    );
    try {
      if (!route.context.stream.writeRaw(frame)) {
        throw new Error(`Actor '${actorId}' local bound session error response failed.`);
      }
      this.routes.requireCurrentToken(actorId, route.bindingToken);
      return true;
    } finally {
      frame.close();
    }
  }


  async sendNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Send,
      resolvePacketName(message, packetName),
      metadata,
      false,
      undefined,
      message
    );
    try {
      await this.sendNativeBoundSessionFrame(node, actorRef, frame, signal);
    } finally {
      frame.close();
    }
  }

  async sendNativeBoundSessionResponse(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const frame = this.frameMessages.createJsonFrameMessage(
      ZLinkStreamMessageKind.Response,
      packetName,
      metadata,
      false,
      requestSeq,
      message
    );
    try {
      await this.sendNativeBoundSessionFrame(node, actorRef, frame, signal);
    } finally {
      frame.close();
    }
  }

  async disconnectNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    await node.closeActorBoundSession(actorRef as unknown as ZLinkBackendActorRef, 0, signal);
  }

  async disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.requireRoute(actorId);
    try {
      await this.requireTransport().disconnect(actorId, {
        bindingToken: route.bindingToken,
        signal
      });
    } finally {
      this.unbind(actorId, route.context, route.bindingToken);
    }
  }

  async relay(
    actor: DefaultZLinkSessionActor,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void> {
    this.routes.requireCurrentToken(actor.actorId, actor.bindingToken);
    const currentHeader = this.routes.requireRoute(actor.actorId).context.dispatchHeader;
    if (currentHeader === undefined) {
      throw new Error('Session actor relay requires an active stream dispatch.');
    }
    const payloadMessage = encodeFrameworkPayloadMessage(payload, this.options.messageSerializers);
    try {
      if (this.options.relay !== undefined) {
        const handled = await this.options.relay(actor, currentHeader, payloadMessage, signal);
        if (handled) {
          return;
        }
      }
      const route = this.routes.requireRoute(actor.actorId);
      if (!(route.context.stream instanceof ZLinkManagedStream)) {
        return;
      }
      const headerMessage = this.frameMessages.createBinaryMessage(encodeStreamHeader(currentHeader));
      const framePayloadMessage = this.frameMessages.createBinaryMessage(messageToBytes(payloadMessage));
      try {
        if (!route.context.stream.sendBoundActor(actor.actorId, [headerMessage, framePayloadMessage], 0)) {
          throw new Error('Actor session relay failed because the session relay route was not ready before timeout.');
        }
      } finally {
        headerMessage.close();
        framePayloadMessage.close();
      }
    } finally {
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

  decompressPayload(payload: Message): Message {
    return simpleMessage(decompressStreamPayload(messageToBytes(payload), this.compressionCodec)) as Message;
  }

  createJsonFrameMessage(
    kind: ZLinkStreamMessageKind,
    packetName: string,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    requestSeq: bigint | undefined,
    payload: unknown,
    correlationId?: string
  ): Message {
    return this.frameMessages.createJsonFrameMessage(kind, packetName, metadata, compressed, requestSeq, payload, correlationId);
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
    const sessionRid = context.routingId;
    if (sessionRid === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor session binding requires a stream routing id.'
      );
    }
    await context.stream.bindActor(actorRef, this.options.actorBindTimeoutMs ?? 2000, signal);
  }

  private relayRemoteBoundSessionBind(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef
  ): void {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    const header = ZLinkBindingMessage.from(Buffer.from(encodeStreamHeader({
      kind: ZLinkStreamMessageKind.Send,
      codec: ZLinkStreamCodec.Raw,
      flags: ZLinkStreamHeaderFlags.None,
      name: REMOTE_BOUND_SESSION_BIND_PACKET,
      metadata: new Map()
    })));
    const body = ZLinkBindingMessage.from(Buffer.alloc(0));
    try {
      if (!context.stream.sendBoundActor(actorRef.actorId, [header, body], 0)) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actorRef.actorId}' remote bound session bind relay failed.`
        );
      }
    } finally {
      header.close();
      body.close();
    }
  }

  private async sendNativeBoundSessionFrame(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    frame: Message,
    signal?: AbortSignal
  ): Promise<void> {
    const deadline = Date.now() + (this.options.actorBindTimeoutMs ?? 2000);
    const backendActorRef = toBoundSessionSendActorRef(actorRef);
    let lastError: unknown;
    do {
      throwIfAborted(signal);
      try {
        if (node.sendActorBoundSession(backendActorRef, [frame], ZLINK_SEND_DONT_WAIT)) {
          return;
        }
        lastError = undefined;
      } catch (error) {
        if (!isNativeBoundSessionSendRetryable(error)) {
          throw error;
        }
        lastError = error;
      }
      await delayNativeBoundSessionRetry(deadline, signal);
    } while (Date.now() <= deadline);

    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `Actor '${actorRef.actorId}' bound session route is not ready.`,
      false,
      lastError
    );
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

  route(actorId: string): ZLinkActorSessionRoute | undefined {
    return this.routes.get(actorId);
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined || route.context !== context || route.bindingToken !== bindingToken) {
      return;
    }
    this.routes.delete(actorId);
    context.unbindLocal(actorId, bindingToken);
  }

  unbindActor(actorId: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined) {
      return;
    }
    this.unbind(actorId, route.context, route.bindingToken);
  }

  cleanup(context: DefaultZLinkSessionContext): void {
    for (const route of [...this.routes.values()]) {
      if (route.context === context) {
        this.unbind(route.actor.actorId, context, route.bindingToken);
      }
    }
  }

  requireRoute(actorId: string): ZLinkActorSessionRoute {
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
    const route = this.requireRoute(actorId);
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
  private readonly compressionCodec: ZLinkStreamCompressionCodec | undefined;

  constructor(private readonly options: ZLinkStreamBindingRuntimeOptions) {
    this.compressionCodec = resolveStreamCompressionCodec(options.streamCompression);
  }

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
    payload: unknown,
    correlationId?: string
  ): Message {
    const encoded = this.encodePayload(payload);
    let body = encoded.payload;
    if (compressed) {
      body = compressStreamPayload(body, this.compressionCodec);
    }
    const flags = compressed ? ZLinkStreamHeaderFlags.PayloadCompressed : ZLinkStreamHeaderFlags.None;
    const frame = encodeStreamFrame(
      {
        kind,
        codec: encoded.codec,
        flags,
        requestSeq,
        name: packetName,
        metadata,
        correlationId
      },
      body
    );
    return this.createBinaryMessage(frame);
  }

  private encodePayload(payload: unknown): { codec: ZLinkStreamCodec; payload: Uint8Array } {
    const codec = this.options.streamPayloadCodec;
    if (codec !== undefined) {
      return codec.encode(payload);
    }
    return {
      codec: ZLinkStreamCodec.Json,
      payload: utf8Encode(JSON.stringify(payload))
    };
  }

  private requireMessageFactory(): ZLinkStreamMessageFactory {
    return this.options.messageFactory ?? defaultStreamMessageFactory;
  }
}

export const zlinkStreamLz4CompressionCodec: ZLinkStreamCompressionCodec = {
  compress(payload: Uint8Array): Uint8Array {
    return lz4Pickle(payload);
  },
  decompress(payload: Uint8Array, maxDecompressedSize: number): Uint8Array {
    return lz4Unpickle(payload, maxDecompressedSize);
  }
};

function resolveStreamCompressionCodec(
  options: ZLinkStreamCompressionOptions | undefined
): ZLinkStreamCompressionCodec | undefined {
  if (options?.disabled === true) {
    return undefined;
  }
  return options?.codec ?? zlinkStreamLz4CompressionCodec;
}

function compressStreamPayload(
  payload: Uint8Array,
  codec: ZLinkStreamCompressionCodec | undefined
): Uint8Array {
  if (codec === undefined) {
    throw new Error('Compression codec is not configured.');
  }
  try {
    return codec.compress(payload);
  } catch (error) {
    throw new Error(`Compression failed: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function decompressStreamPayload(
  payload: Uint8Array,
  codec: ZLinkStreamCompressionCodec | undefined,
  maxDecompressedSize = DEFAULT_MAX_DECOMPRESSED_STREAM_PAYLOAD_SIZE
): Uint8Array {
  if (codec === undefined) {
    throw new Error('Compression codec is not configured.');
  }
  let decompressed: Uint8Array;
  try {
    decompressed = codec.decompress(payload, maxDecompressedSize);
  } catch (error) {
    throw new Error(`Decompression failed: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (decompressed.length > maxDecompressedSize) {
    throw new Error('Decompressed stream payload exceeds maximum stream payload size.');
  }
  return decompressed;
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
    readonly stream: ZLinkManagedStream,
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
    payload: unknown,
    correlationId?: string
  ): Message {
    return this.runtime.createJsonFrameMessage(
      kind,
      packetName,
      metadata,
      compressed,
      requestSeq,
      payload,
      correlationId
    );
  }

  enterDispatch(header: ZLinkStreamFrameHeader): void {
    this.currentDispatchHeader = header;
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

  tryCompleteResponse(header: ZLinkStreamFrameHeader, payload: Message): boolean {
    if (header.requestSeq === undefined) {
      return false;
    }
    if (header.kind === ZLinkStreamMessageKind.Response) {
      return this.requests.complete(header.requestSeq, payload);
    }
    if (header.kind === ZLinkStreamMessageKind.Error) {
      return this.requests.fail(header.requestSeq, decodeStreamErrorPayload(payload));
    }
    return false;
  }

  payloadForHeader(header: ZLinkStreamFrameHeader, payload: Message): Message {
    if ((header.flags & ZLinkStreamHeaderFlags.PayloadCompressed) === 0) {
      return payload;
    }
    return this.runtime.decompressPayload(payload);
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

  send(message: unknown): ZLinkSessionSendCall {
    return new DefaultZLinkSessionSendCall(this.context, message);
  }

  reply(message: unknown): ZLinkSessionReplyCall {
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

  relay(payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    return this.runtime.relay(this, payload, signal);
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

  send(message: unknown): ZLinkBoundSessionSendCall {
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

  fail(error: unknown): void {
    if (this.completed) {
      return;
    }
    this.completed = true;
    this.clearTimeout();
    this.rejectPromise(error);
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

  fail(requestSeq: bigint, error: unknown): boolean {
    const pending = this.pending.get(requestSeq);
    if (pending === undefined) {
      return false;
    }
    this.pending.delete(requestSeq);
    pending.fail(error);
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

function decodeStreamErrorPayload(payload: Message): Error {
  try {
    const value = JSON.parse(Buffer.from(messageToBytes(payload)).toString()) as {
      readonly code?: unknown;
      readonly message?: unknown;
    };
    return new Error(typeof value.message === 'string' ? value.message : 'Stream request failed.');
  } catch {
    return new Error('Stream request failed.');
  }
}

class DefaultZLinkBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly actorId: string,
    private readonly message: unknown
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

class DefaultZLinkSessionSendCall implements ZLinkSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: unknown
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
      if (!this.context.stream.writeRaw(message)) {
        throw new Error('Client stream send failed.');
      }
    } finally {
      message.close();
    }
  }
}

class DefaultZLinkSessionReplyCall implements ZLinkSessionReplyCall {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;
  private executed = false;

  constructor(
    private readonly context: DefaultZLinkSessionContext,
    private readonly message: unknown
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
      this.message,
      requestHeader.correlationId
    );
    try {
      if (!this.context.stream.writeRaw(message)) {
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
    && 'nodeRid' in value
    && 'generation' in value
    && 'actorId' in value
  );
}

function createBindingToken(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

function sameActorRef(left: ActorRef, right: ActorRef): boolean {
  return String(left.nodeRid) === String(right.nodeRid)
    && left.actorId === right.actorId
    && BigInt(left.generation) === BigInt(right.generation);
}

function toBackendActorRef(actor: ActorRef): ZLinkBackendActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: actor.generation
  };
}

function toBoundSessionSendActorRef(actor: ActorRef): ZLinkBackendActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: 0n
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
    value() {
      return JSON.parse(utf8Decode(copy));
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
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
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
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

async function delayNativeBoundSessionRetry(deadline: number, signal: AbortSignal | undefined): Promise<void> {
  const remaining = deadline - Date.now();
  if (remaining <= 0) {
    return;
  }
  await new Promise<void>((resolve) =>
    setTimeout(resolve, Math.min(ZLINK_NATIVE_BOUND_SESSION_RETRY_DELAY_MS, remaining))
  );
  throwIfAborted(signal);
}

function isNativeBoundSessionSendRetryable(error: unknown): boolean {
  return error instanceof SubmitError &&
    (error.result === SubmitResult.Backpressured || error.result === SubmitResult.NotConnected);
}
