import type {
  ZLinkProviderResolver,
  ZLinkMessageSerializer,
  ZLinkSession,
  ZLinkSessionDispatchContext
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSocketNativeEventType
} from '../../contracts';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkRuntimeDispatchErrorAction as ZLinkDispatchErrorAction,
  ZLinkRuntimeDispatchErrorReason as ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import type { Message } from '../../contracts/Common/Message';
import { throwIfAborted } from '../abort';
import { ZLinkDispatchErrorReporter, ZLinkRouteDisconnectedError } from '../channels';
import { flowIfEnabled } from '../diagnostics';
import type { ZLinkRuntimeMetrics } from '../diagnostics';
import { wrapFrameworkPayloadMessage } from '../messaging/payload-codec';
import type {
  ZLinkBackendSocketMonitor,
  ZLinkBackendSocketMonitorEvent,
  ZLinkBackendStreamSocket
} from '../backend/contracts';
import type { ZLinkMeshCompletionTable } from '../backend/mesh-completion-table';
import type { StreamSessionService } from '../foundation/service-runtime-contracts';
import {
  decodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader,
  ZLINK_STREAM_HEARTBEAT_PING,
  ZLINK_STREAM_HEARTBEAT_PONG,
  ZLinkStreamCloseReasonCode,
  ZLinkStreamMessageKind
} from './protocol';
import { createInboundFlow, runWithFlow } from '../diagnostics/flow-context';
import {
  streamSessionIdFromRoutingId,
  ZLinkManagedStream
} from './managed-stream';
import { DefaultZLinkSessionContext } from './session-context';
import { ZLinkStreamSessionSerialExecutor } from './session-serial-executor';
import type { ZLinkApplicationWorkClaim } from '../admission';
import { ZLinkAsyncSubmitter } from '../messaging';

const ZLINK_SEND_DONT_WAIT = 1;
const ZLINK_STREAM_HEARTBEAT_INTERVAL_MS = 1_000;
const ZLINK_STREAM_HEARTBEAT_TIMEOUT_MS = 5_000;
const ZLINK_STREAM_APPLICATION_IDLE_TIMEOUT_MS = 30_000;

interface ZLinkStreamLivenessClock {
  now(): number;
  setTimer(callback: () => void, delayMs: number): unknown;
  clearTimer(timer: unknown): void;
}

interface ZLinkStreamLivenessOptions {
  readonly livenessClock?: ZLinkStreamLivenessClock;
  readonly acceptNewSession?: () => boolean;
  readonly claimApplicationWork?: () => ZLinkApplicationWorkClaim;
}

const systemLivenessClock: ZLinkStreamLivenessClock = {
  now: () => Date.now(),
  setTimer: (callback, delayMs) => setTimeout(callback, delayMs),
  clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout>)
};

export interface ZLinkStreamSessionContextFactory {
  createSessionContext(
    stream: ZLinkManagedStream,
    close?: (signal?: AbortSignal) => Promise<void>,
    providerResolver?: ZLinkProviderResolver
  ): DefaultZLinkSessionContext;
}

export interface ZLinkStreamSessionRuntimeOptions {
  readonly socket: ZLinkBackendStreamSocket;
  readonly nativeSessionService?: StreamSessionService;
  readonly meshCompletions?: ZLinkMeshCompletionTable;
  readonly sessionFactory: (context: DefaultZLinkSessionContext) => ZLinkSession | Promise<ZLinkSession>;
  readonly bindingRuntime: ZLinkStreamSessionContextFactory;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly onError?: (error: unknown) => void;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly metrics?: ZLinkRuntimeMetrics;
  readonly submitter?: ZLinkAsyncSubmitter;
}

export interface ZLinkStreamSessionNodeRuntimeOptions extends ZLinkStreamSessionRuntimeOptions {
  readonly nodeName?: string;
  readonly monitor?: ZLinkBackendSocketMonitor;
}

function createDispatchContext(header: ZLinkStreamFrameHeader): ZLinkSessionDispatchContext {
  return {
    packetName: header.name,
    metadata: header.metadata,
    canReply: header.requestSeq !== undefined
  };
}

export class ZLinkStreamSessionRuntime {
  readonly stream: ZLinkManagedStream;
  readonly context: DefaultZLinkSessionContext;
  private readonly sessionReady: Promise<ZLinkSession>;
  private readonly serial = new ZLinkStreamSessionSerialExecutor();
  private connected = false;
  private disconnected = false;
  private disposed = false;
  private closeReason = 'client_close';
  private metricsClosed = false;
  private readonly livenessClock: ZLinkStreamLivenessClock;
  private lastApplicationActivityAt = 0;
  private awaitingPongSince: number | undefined;
  private livenessTimer: unknown;

  constructor(
    private readonly options: ZLinkStreamSessionRuntimeOptions & ZLinkStreamLivenessOptions,
    routingId: unknown,
    private readonly removeSession: (sessionId: string, session: ZLinkStreamSessionRuntime) => void = () => {}
  ) {
    this.livenessClock = options.livenessClock ?? systemLivenessClock;
    this.stream = new ZLinkManagedStream(
      options.socket,
      routingId,
      options.messageSerializers,
      options.nativeSessionService,
      options.meshCompletions,
      options.submitter
    );
    this.context = options.bindingRuntime.createSessionContext(
      this.stream,
      (signal) => this.close(signal),
      options.providerResolver
    );
    const sessionOrPromise = options.sessionFactory(this.context);
    this.sessionReady = isPromiseLike(sessionOrPromise)
      ? sessionOrPromise.then(
        (session) => this.completeSessionCreation(session),
        (error) => {
          this.context.completeConfiguration();
          throw error;
        }
      )
      : Promise.resolve(this.completeSessionCreation(sessionOrPromise));
  }

  get session(): Promise<ZLinkSession> {
    return this.sessionReady;
  }

  get isDisconnected(): boolean {
    return this.disconnected;
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

  private completeSessionCreation(session: ZLinkSession): ZLinkSession {
    this.context.completeConfiguration();
    return this.requireProvidedContext(session);
  }

  private async requireSession(): Promise<ZLinkSession> {
    const session = await this.sessionReady;
    return session;
  }

  enqueueConnected(localAddr?: string, remoteAddr?: string): void {
    this.enqueue(async () => this.markConnected(localAddr, remoteAddr));
  }

  enqueuePacket(header: Message, payload: Message): void {
    this.enqueueApplication(
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
    this.stopLivenessChecks();
    this.enqueue(async () => this.complete(error, true));
  }

  async close(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await this.stream.close(signal);
    if (this.disconnected) {
      return;
    }
    this.disconnected = true;
    this.stopLivenessChecks();
    this.enqueue(async () => this.complete(undefined, true));
  }

  async dispose(): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.stopLivenessChecks();
    await this.serial.dispose();
    if (!this.disconnected) {
      this.disconnected = true;
      const session = await this.requireSession();
      await session.onDisconnected?.(this.context);
    }
    await this.cleanup();
  }

  async drainClose(): Promise<void> {
    this.closeReason = 'server_drain';
    await this.serial.run(async () => {
      if (this.disposed) return;
      this.stopLivenessChecks();
      await this.stream.closeForDrain();
      if (!this.disconnected) {
        this.disconnected = true;
        await this.complete(undefined, true);
      }
    });
  }

  private async markConnected(localAddr?: string, remoteAddr?: string): Promise<void> {
    this.stream.updateAddresses(localAddr, remoteAddr);
    if (this.connected) {
      return;
    }
    this.connected = true;
    this.lastApplicationActivityAt = this.livenessClock.now();
    this.scheduleLivenessCheck();
    this.options.metrics?.change('zlink.stream.connections.active', 1);
    this.options.metrics?.count('zlink.stream.connections.opened');
    const session = await this.requireSession();
    await session.onConnected?.(this.context);
  }

  private async dispatchPacket(header: Message, payload: Message): Promise<void> {
    let decodedHeader: ZLinkStreamFrameHeader | undefined;
    let dispatchPayload = payload;
    let enteredDispatch = false;
    try {
      decodedHeader = decodeStreamHeader(messageToBytes(header));
      if (decodedHeader.kind === ZLinkStreamMessageKind.Control) {
        await this.handleControl(decodedHeader, payload);
        return;
      }
      this.lastApplicationActivityAt = this.livenessClock.now();
      dispatchPayload = this.context.payloadForHeader(decodedHeader, payload);
      if (this.context.tryCompleteResponse(decodedHeader, dispatchPayload)) {
        return;
      }
      this.context.enterDispatch(decodedHeader);
      enteredDispatch = true;
      const session = await this.requireSession();
      const streamKind = decodedHeader.kind === ZLinkStreamMessageKind.Request
        ? ZLinkDispatchMessageKind.Request
        : ZLinkDispatchMessageKind.Send;
      const streamCorr = decodedHeader.correlationId;
      const inboundHeader = decodedHeader;
      await runWithFlow(createInboundFlow(
        inboundHeader.flowId,
        inboundHeader.flowOrigin,
        this.options.dispatchErrors?.flow.flowCreationEnabled() ?? true
      ), async () => {
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Received)?.trace({
          outcome: ZLinkMessageFlowOutcome.Received,
          surface: ZLinkDispatchErrorSurface.StreamSession,
          messageKind: streamKind,
          packetName: inboundHeader.name,
          correlationId: streamCorr,
          sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId)
        });
        await session.onDispatch?.(
          createDispatchContext(inboundHeader),
          wrapFrameworkPayloadMessage(dispatchPayload, this.options.messageSerializers)
        );
        flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Dispatched)?.trace({
          outcome: ZLinkMessageFlowOutcome.Dispatched,
          surface: ZLinkDispatchErrorSurface.StreamSession,
          messageKind: streamKind,
          packetName: inboundHeader.name,
          correlationId: streamCorr,
          sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId)
        });
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
        correlationId: decodedHeader?.correlationId,
        flowId: decodedHeader?.flowId,
        flowOrigin: decodedHeader?.flowOrigin,
        error
      });
      this.options.onError?.(error);
      await this.replyDispatchError(decodedHeader, error);
      if (error instanceof ZLinkRouteDisconnectedError && decodedHeader?.requestSeq === undefined) {
        await this.context.close();
      }
    } finally {
      if (enteredDispatch) {
        this.context.exitDispatch();
      }
      header.close();
      if (dispatchPayload !== payload) {
        dispatchPayload.close();
      }
      payload.close();
    }
  }

  private async handleControl(header: ZLinkStreamFrameHeader, payload: Message): Promise<void> {
    if (messageToBytes(payload).length !== 0) {
      await this.closeForLiveness(
        ZLinkStreamCloseReasonCode.ProtocolError,
        'protocol_error',
        'Control heartbeat payload must be empty.'
      );
      return;
    }
    if (header.name === ZLINK_STREAM_HEARTBEAT_PONG) {
      this.awaitingPongSince = undefined;
      return;
    }
    if (header.name === ZLINK_STREAM_HEARTBEAT_PING) {
      if (!this.stream.writeControl(ZLINK_STREAM_HEARTBEAT_PONG)) {
        await this.closeForLiveness(
          ZLinkStreamCloseReasonCode.TransportError,
          'transport_error',
          'Heartbeat pong send failed.'
        );
      }
      return;
    }
    await this.closeForLiveness(
      ZLinkStreamCloseReasonCode.ProtocolError,
      'protocol_error',
      `Unknown stream control frame: ${header.name}`
    );
  }

  private scheduleLivenessCheck(): void {
    if (this.disposed || this.disconnected || this.livenessTimer !== undefined) {
      return;
    }
    this.livenessTimer = this.livenessClock.setTimer(() => {
      this.livenessTimer = undefined;
      this.enqueue(async () => this.runLivenessCheck());
    }, ZLINK_STREAM_HEARTBEAT_INTERVAL_MS);
  }

  private async runLivenessCheck(): Promise<void> {
    if (this.disposed || this.disconnected || !this.connected) {
      return;
    }
    const now = this.livenessClock.now();
    if (now - this.lastApplicationActivityAt >= ZLINK_STREAM_APPLICATION_IDLE_TIMEOUT_MS) {
      await this.closeForLiveness(
        ZLinkStreamCloseReasonCode.IdleTimeout,
        'idle_timeout',
        'Application idle timeout.'
      );
      return;
    }
    if (
      this.awaitingPongSince !== undefined
      && now - this.awaitingPongSince >= ZLINK_STREAM_HEARTBEAT_TIMEOUT_MS
    ) {
      await this.closeForLiveness(
        ZLinkStreamCloseReasonCode.HeartbeatTimeout,
        'heartbeat_timeout',
        'Heartbeat timeout.'
      );
      return;
    }
    if (!this.stream.writeControl(ZLINK_STREAM_HEARTBEAT_PING)) {
      await this.closeForLiveness(
        ZLinkStreamCloseReasonCode.TransportError,
        'transport_error',
        'Heartbeat ping send failed.'
      );
      return;
    }
    this.awaitingPongSince ??= now;
    this.scheduleLivenessCheck();
  }

  private async closeForLiveness(
    reason: ZLinkStreamCloseReasonCode,
    closeReason: string,
    diagnostic: string
  ): Promise<void> {
    if (this.disconnected) {
      return;
    }
    this.disconnected = true;
    this.closeReason = closeReason;
    this.stopLivenessChecks();
    await this.stream.closeForReason(reason, diagnostic);
    await this.complete(undefined, true);
  }

  private stopLivenessChecks(): void {
    if (this.livenessTimer !== undefined) {
      this.livenessClock.clearTimer(this.livenessTimer);
      this.livenessTimer = undefined;
    }
  }

  private async replyDispatchError(header: ZLinkStreamFrameHeader | undefined, error: unknown): Promise<void> {
    if (header?.requestSeq === undefined) {
      return;
    }
    const message = this.context.createJsonReplyFrameMessage(
      header,
      ZLinkStreamMessageKind.Error,
      new Map(),
      false,
      {
        code: error instanceof Error ? error.constructor.name : undefined,
        message: error instanceof Error ? error.message : String(error)
      }
    );
    try {
      if (!this.context.stream.writeRaw(message, ZLINK_SEND_DONT_WAIT)) {
        throw new Error('Client stream error reply send failed.');
      }
      flowIfEnabled(this.options.dispatchErrors?.flow, ZLinkMessageFlowOutcome.Sent)?.trace({
        outcome: ZLinkMessageFlowOutcome.Sent,
        surface: ZLinkDispatchErrorSurface.StreamSession,
        messageKind: ZLinkDispatchMessageKind.Request,
        packetName: header.name,
        correlationId: header.correlationId,
        sourceRid: this.context.routingId === undefined ? undefined : String(this.context.routingId)
      });
    } catch (replyError) {
      this.options.onError?.(replyError);
    } finally {
      message.close();
    }
  }

  private async complete(error: unknown, notifyDisconnected: boolean): Promise<void> {
    if (error !== undefined && this.closeReason !== 'server_drain') {
      this.closeReason = 'transport_error';
    }
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
    if (this.connected && !this.metricsClosed) {
      this.metricsClosed = true;
      this.options.metrics?.change('zlink.stream.connections.active', -1);
      this.options.metrics?.count('zlink.stream.connections.closed', 1, { close_reason: this.closeReason });
    }
    await this.context.cleanupBindings();
    this.removeSession(this.stream.sessionId, this);
  }

  private enqueue(work: () => Promise<void>, onRejected?: () => void): void {
    if (!this.serial.enqueue(work)) {
      onRejected?.();
    }
  }

  private enqueueApplication(work: () => Promise<void>, onRejected?: () => void): void {
    let claim: ZLinkApplicationWorkClaim | undefined;
    try {
      claim = this.options.claimApplicationWork?.();
    } catch {
      onRejected?.();
      return;
    }
    this.enqueue(async () => {
      try {
        await work();
      } finally {
        claim?.close();
      }
    }, () => {
      claim?.close();
      onRejected?.();
    });
  }
}

export class ZLinkStreamSessionNodeRuntime {
  private readonly sessions = new Map<string, ZLinkStreamSessionRuntime>();
  private readonly pendingConnectionMetadata: Array<{
    readonly localAddr?: string;
    readonly remoteAddr?: string;
  }> = [];
  private readonly unaddressedMonitorSessions: string[] = [];
  private readonly disconnectedEndpoints = new Set<string>();
  private pendingEndpointlessDisconnect:
    | {
        readonly session: ZLinkStreamSessionRuntime;
        cancelled: boolean;
        readonly error: Error;
        readonly activityVersion: number;
      }
    | undefined;
  private activityVersion = 0;
  private stopped = false;
  private readonly submitter: ZLinkAsyncSubmitter;

  constructor(
    private readonly options: ZLinkStreamSessionNodeRuntimeOptions & ZLinkStreamLivenessOptions
  ) {
    this.submitter = options.submitter ?? new ZLinkAsyncSubmitter(
      (handler) => options.socket.onSendReady(handler),
      { timeoutMs: options.socket.sendTimeoutMs > 0 ? options.socket.sendTimeoutMs : 1000 }
    );
  }

  start(): void {
    this.options.socket.onFramedPacket((routingId, header, payload) => {
      this.onFramedPacket(routingId, header, payload);
    });
    this.options.monitor?.onEvent((event) => {
      this.onMonitorEvent(event);
    });
  }

  markConnected(routingId: unknown, localAddr?: string, remoteAddr?: string): void {
    const session = this.getOrCreateSession(routingId);
    session?.enqueueConnected(localAddr, remoteAddr);
  }

  markDisconnected(routingId: unknown, error?: unknown): void {
    this.sessions.get(streamSessionIdFromRoutingId(routingId))?.enqueueDisconnected(error);
  }

  findSession(routingId: unknown): ZLinkStreamSessionRuntime | undefined {
    return this.sessions.get(streamSessionIdFromRoutingId(routingId));
  }

  async dispose(): Promise<void> {
    this.stopped = true;
    this.submitter.dispose();
    const sessions = [...this.sessions.values()];
    this.sessions.clear();
    this.unaddressedMonitorSessions.length = 0;
    for (const session of sessions) {
      await session.dispose();
    }
  }

  async drainCloseSessions(): Promise<void> {
    await Promise.allSettled([...this.sessions.values()].map((session) => session.drainClose()));
  }

  private onFramedPacket(routingId: unknown, header: Message, payload: Message): void {
    if (this.stopped) {
      header.close();
      payload.close();
      return;
    }
    this.activityVersion += 1;
    const session = this.getOrCreateSession(routingId);
    if (session === undefined) {
      header.close();
      payload.close();
      return;
    }
    this.applyPendingConnectionMetadata(session);
    session.enqueuePacket(header, payload);
  }

  private onMonitorEvent(event: ZLinkBackendSocketMonitorEvent): void {
    if (this.stopped) {
      return;
    }
    switch (event.nativeEvent) {
      case ZLinkSocketNativeEventType.ConnectionReady:
        this.activityVersion += 1;
        {
          const endpointKey = streamMonitorEndpointKey(event.localAddr, event.remoteAddr);
          if (this.disconnectedEndpoints.delete(endpointKey)) {
            return;
          }
        }
        if (event.routingId === undefined) {
          const endpointKey = streamMonitorEndpointKey(event.localAddr, event.remoteAddr);
          const unaddressed = this.firstUnaddressedSession();
          if (unaddressed !== undefined) {
            this.disconnectedEndpoints.delete(endpointKey);
            unaddressed.enqueueConnected(event.localAddr, event.remoteAddr);
            this.unaddressedMonitorSessions.push(unaddressed.stream.sessionId);
            return;
          }
          if (this.pendingConnectionMetadata.some((metadata) =>
            streamMonitorEndpointKey(metadata.localAddr, metadata.remoteAddr) === endpointKey
          )) {
            return;
          }
          this.pendingConnectionMetadata.push({
            localAddr: event.localAddr,
            remoteAddr: event.remoteAddr
          });
          return;
        }
        this.getOrCreateSession(event.routingId)?.enqueueConnected(event.localAddr, event.remoteAddr);
        return;
      case ZLinkSocketNativeEventType.Disconnected:
        {
          const endpointKey = streamMonitorEndpointKey(event.localAddr, event.remoteAddr);
          this.disconnectedEndpoints.add(endpointKey);
          this.removePendingConnectionMetadata(endpointKey);
          const session = this.resolveMonitorSession(event);
          const error = new Error(`Stream disconnected: ${event.nativeEvent}/${event.value}`);
          if (session !== undefined) {
            session.enqueueDisconnected(error);
            return;
          }
          if (
            event.routingId === undefined
            && !streamMonitorHasEndpoint(event)
          ) {
            this.enqueueEndpointlessDisconnect(error);
          }
        }
        return;
      default:
        return;
    }
  }

  private applyPendingConnectionMetadata(session: ZLinkStreamSessionRuntime): void {
    if (
      session.stream.localAddr !== undefined
      || session.stream.remoteAddr !== undefined
      || this.pendingConnectionMetadata.length === 0
    ) {
      return;
    }
    const metadata = this.pendingConnectionMetadata.shift()!;
    this.removePendingConnectionMetadata(streamMonitorEndpointKey(metadata.localAddr, metadata.remoteAddr));
    session.enqueueConnected(metadata.localAddr, metadata.remoteAddr);
    this.unaddressedMonitorSessions.push(session.stream.sessionId);
  }

  private firstUnaddressedSession(): ZLinkStreamSessionRuntime | undefined {
    return [...this.sessions.values()].find((session) =>
      !session.isDisconnected
      && session.stream.localAddr === undefined
      && session.stream.remoteAddr === undefined
    );
  }

  private activeSessions(): ZLinkStreamSessionRuntime[] {
    return [...this.sessions.values()].filter((session) => !session.isDisconnected);
  }

  private getActiveSession(sessionId: string): ZLinkStreamSessionRuntime | undefined {
    const session = this.sessions.get(sessionId);
    if (session === undefined || session.isDisconnected) {
      return undefined;
    }
    return session;
  }

  private removePendingConnectionMetadata(endpointKey: string): void {
    for (let index = this.pendingConnectionMetadata.length - 1; index >= 0; index--) {
      const metadata = this.pendingConnectionMetadata[index];
      if (streamMonitorEndpointKey(metadata.localAddr, metadata.remoteAddr) === endpointKey) {
        this.pendingConnectionMetadata.splice(index, 1);
      }
    }
  }

  private resolveMonitorSession(event: ZLinkBackendSocketMonitorEvent): ZLinkStreamSessionRuntime | undefined {
    if (event.routingId !== undefined) {
      const session = this.getActiveSession(streamSessionIdFromRoutingId(event.routingId));
      if (session !== undefined) {
        return session;
      }
    }
    const activeSessions = this.activeSessions();
    if (streamMonitorHasEndpoint(event)) {
      const matches = activeSessions.filter((session) =>
        session.stream.localAddr === event.localAddr
        && session.stream.remoteAddr === event.remoteAddr
      );
      return matches.length === 1 ? matches[0] : undefined;
    }
    return undefined;
  }

  private enqueueEndpointlessDisconnect(error: Error): void {
    const sessionId = this.unaddressedMonitorSessions.shift();
    if (sessionId !== undefined) {
      this.getActiveSession(sessionId)?.enqueueDisconnected(error);
      return;
    }
    const sessions = this.activeSessions();
    if (sessions.length !== 1) {
      return;
    }
    if (this.pendingEndpointlessDisconnect !== undefined) {
      this.pendingEndpointlessDisconnect.cancelled = true;
    }
    const pending = {
      session: sessions[0],
      cancelled: false,
      error,
      activityVersion: this.activityVersion
    };
    this.pendingEndpointlessDisconnect = pending;
    setImmediate(() => {
      if (
        pending.cancelled
        || this.stopped
        || this.pendingEndpointlessDisconnect !== pending
        || this.activityVersion !== pending.activityVersion
        || this.sessions.get(pending.session.stream.sessionId) !== pending.session
      ) {
        return;
      }
      this.pendingEndpointlessDisconnect = undefined;
      pending.session.enqueueDisconnected(pending.error);
    });
  }

  private getOrCreateSession(routingId: unknown): ZLinkStreamSessionRuntime | undefined {
    const sessionId = streamSessionIdFromRoutingId(routingId);
    const existing = this.getActiveSession(sessionId);
    if (existing !== undefined) {
      return existing;
    }
    if (this.options.acceptNewSession?.() === false) {
      const rejected = new ZLinkManagedStream(
        this.options.socket,
        routingId,
        this.options.messageSerializers,
        this.options.nativeSessionService,
        this.options.meshCompletions
      );
      void rejected.closeForDrain().catch((error) => this.options.onError?.(error));
      return undefined;
    }
    const created = new ZLinkStreamSessionRuntime(
      { ...this.options, submitter: this.submitter },
      routingId,
      (sessionId, session) => {
        if (this.sessions.get(sessionId) === session) {
          this.sessions.delete(sessionId);
        }
      }
    );
    this.sessions.set(sessionId, created);
    return created;
  }
}

function streamMonitorEndpointKey(localAddr: string | undefined, remoteAddr: string | undefined): string {
  return `${localAddr ?? ''}\n${remoteAddr ?? ''}`;
}

function streamMonitorHasEndpoint(event: ZLinkBackendSocketMonitorEvent): boolean {
  const endpoint = event as { readonly localAddr?: string; readonly remoteAddr?: string };
  return endpoint.localAddr !== undefined || endpoint.remoteAddr !== undefined;
}

function isPromiseLike<T>(value: T | Promise<T>): value is Promise<T> {
  return typeof (value as { then?: unknown }).then === 'function';
}
