import type {
  ActorRef,
  Type,
  ZLinkActor,
  ZLinkBoundSession,
  ZLinkSessionActor,
  ZLinkSessionFactory,
  ZLinkSession,
  ZLinkProviderResolver,
  ZLinkMessageSerializer,
  ZLinkStreamCompressionCodec,
  ZLinkStreamCompressionOptions
} from '../../contracts';
import {
  ZLinkMessage
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkDispatchErrorReporter } from '../channels';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSpotNode,
  ZLinkBackendStreamSocket
} from '../backend/contracts';
import {
  messageToBytes,
  ZLinkStreamCodec,
  type ZLinkStreamFrameHeader,
  ZLinkStreamMessageKind,
  type ZLinkStreamReplyMessageKind
} from './protocol';
import {
  ZLinkActorSessionBindingRegistry
} from './actor-session-binding-registry';
import { ZLinkActorSessionLifecycleCoordinator } from './actor-session-lifecycle-coordinator';
import {
  decompressStreamPayload,
  resolveStreamCompressionCodec,
  ZLinkStreamFrameMessageFactory
} from './stream-frame-factory';
import { simpleMessage } from './stream-message-utils';
import {
  DefaultZLinkBoundSession,
  DefaultZLinkSessionActor,
  DefaultZLinkSessionContext
} from './session-context';
import {
  DefaultZLinkBoundSessionResponseTarget,
  type ZLinkBoundSessionResponseTarget
} from './bound-session-response-target';
import {
  ZLinkBoundSessionService,
  type ZLinkBoundSessionTransport
} from './bound-session-service';
import {
  ZLinkSessionActorCoordinator
} from './session-actor-coordinator';
import {
  ZLinkBoundActorRelaySender
} from './bound-actor-relay-sender';
import {
  ZLinkManagedStream
} from './managed-stream';
import { createStreamSessionInstance } from './session-provider';
import {
  ZLinkStreamSessionNodeRuntime as ZLinkStreamSessionNodeRuntimeCore,
  ZLinkStreamSessionRuntime as ZLinkStreamSessionRuntimeCore,
  type ZLinkStreamSessionNodeRuntimeOptions as ZLinkStreamSessionNodeRuntimeCoreOptions,
  type ZLinkStreamSessionRuntimeOptions as ZLinkStreamSessionRuntimeCoreOptions
} from './stream-session-runtime';
export { ZLinkPendingSessionRequest } from './session-requests';
export { ZLinkActorSessionLifecycleCoordinator } from './actor-session-lifecycle-coordinator';
export { zlinkStreamLz4CompressionCodec } from './stream-frame-factory';
export { ZLinkManagedStream } from './managed-stream';
export {
  DefaultZLinkBoundSession,
  DefaultZLinkBoundSessionFactory,
  DefaultZLinkSessionActor,
  DefaultZLinkSessionContext
} from './session-context';
export type { ZLinkBoundSessionResponseTarget } from './bound-session-response-target';
export type {
  ZLinkBoundSessionDisconnectOptions,
  ZLinkBoundSessionSendOptions,
  ZLinkBoundSessionTransport
} from './bound-session-service';

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
  readonly flowCreationEnabled?: () => boolean;
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

export interface ZLinkStreamSessionRuntimeOptions extends Omit<ZLinkStreamSessionRuntimeCoreOptions, 'bindingRuntime'> {
  readonly bindingRuntime?: ZLinkStreamBindingRuntime;
}

export interface ZLinkStreamSessionNodeRuntimeOptions extends Omit<ZLinkStreamSessionNodeRuntimeCoreOptions, 'bindingRuntime'> {
  readonly bindingRuntime?: ZLinkStreamBindingRuntime;
}

export interface ZLinkStreamRuntimeManagerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  readonly context: ZLinkBackendContext;
  readonly bindingRuntime: ZLinkStreamBindingRuntime;
  readonly spotNodes?: ReadonlyMap<string, ZLinkBackendSpotNode>;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
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
      const tlsServer = streamNode.tlsServer;
      if (tlsServer !== undefined) {
        socket.setTlsServer(
          tlsServer.certificatePath,
          tlsServer.keyPath,
          tlsServer.requireClientCertificate ?? false
        );
      }
      socket.bind(streamNode.bind!);
      const monitor = monitoringAdapter.openSocketMonitor(socket);
      const sessionType = streamNode.session!;
      const runtime = new ZLinkStreamSessionNodeRuntime({
        nodeName,
        socket,
        monitor,
        bindingRuntime: this.options.bindingRuntime,
        dispatchErrors: this.options.dispatchErrors,
        metrics: this.options.metrics,
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

  async notifyServerDrain(): Promise<void> {
    await Promise.all([...this.nodes.values()].map((node) => node.runtime.drainCloseSessions()));
  }

}

export class ZLinkStreamSessionRuntime extends ZLinkStreamSessionRuntimeCore {
  constructor(
    options: ZLinkStreamSessionRuntimeOptions,
    routingId: unknown,
    removeSession?: (sessionId: string, session: ZLinkStreamSessionRuntime) => void
  ) {
    super(
      {
        ...options,
        bindingRuntime: options.bindingRuntime ?? new ZLinkStreamBindingRuntime()
      },
      routingId,
      removeSession === undefined
        ? undefined
        : (sessionId, session) => removeSession(sessionId, session as unknown as ZLinkStreamSessionRuntime)
    );
  }
}

export class ZLinkStreamSessionNodeRuntime extends ZLinkStreamSessionNodeRuntimeCore {
  constructor(options: ZLinkStreamSessionNodeRuntimeOptions) {
    super({
      ...options,
      bindingRuntime: options.bindingRuntime ?? new ZLinkStreamBindingRuntime()
    });
  }
}

export class ZLinkStreamBindingRuntime {
  private readonly routes: ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>;
  private readonly frameMessages: ZLinkStreamFrameMessageFactory;
  private readonly compressionCodec: ZLinkStreamCompressionCodec | undefined;
  private readonly boundSessions: ZLinkBoundSessionService;
  private readonly sessionActors: ZLinkSessionActorCoordinator;
  private readonly boundActorRelay: ZLinkBoundActorRelaySender;

  constructor(options: ZLinkStreamBindingRuntimeOptions = {}) {
    this.routes = new ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>();
    this.compressionCodec = resolveStreamCompressionCodec(options.streamCompression);
    this.frameMessages = new ZLinkStreamFrameMessageFactory(options);
    this.boundSessions = new ZLinkBoundSessionService(this.routes, this.frameMessages, options);
    const actorSessionLifecycle = new ZLinkActorSessionLifecycleCoordinator();
    this.sessionActors = new ZLinkSessionActorCoordinator(this.routes, this.boundSessions, this, options, actorSessionLifecycle);
    this.boundActorRelay = new ZLinkBoundActorRelaySender(this.routes, this.frameMessages, options, actorSessionLifecycle);
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
    return await this.sessionActors.bind(context, actorOrRef, signal);
  }

  async bindOrGet(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    return await this.sessionActors.bindOrGet(context, actorRef, signal);
  }

  find(actorId: string): DefaultZLinkSessionActor | undefined {
    return this.routes.find(actorId);
  }

  hasBoundSession(actorId: string): boolean {
    return this.routes.find(actorId) !== undefined;
  }

  captureBoundSessionResponseTarget(actor: ZLinkSessionActor): ZLinkBoundSessionResponseTarget | undefined {
    const bindingToken = (actor as { readonly bindingToken?: unknown }).bindingToken;
    if (typeof bindingToken !== 'string') {
      return undefined;
    }
    const route = this.routes.route(actor.actorId);
    if (route === undefined || route.actor !== actor || route.bindingToken !== bindingToken) {
      return undefined;
    }
    return new DefaultZLinkBoundSessionResponseTarget(
      this.frameMessages,
      route.context,
      actor.actorId
    );
  }

  async rebindActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    await this.sessionActors.rebindActor(actorRef, signal);
  }

  async refreshActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    await this.sessionActors.refreshActor(actorRef, signal);
  }

  unbind(actorId: string, context: DefaultZLinkSessionContext, bindingToken: string): void {
    this.routes.unbind(actorId, context, bindingToken);
  }

  unbindActor(actorId: string): void {
    this.routes.unbindActor(actorId);
  }

  async cleanup(context: DefaultZLinkSessionContext): Promise<void> {
    await this.sessionActors.cleanupContext(context);
  }

  async sendBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendBoundSession(actorId, message, packetName, metadata, signal);
  }

  sendLocalBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return this.boundSessions.sendLocalBoundSession(actorId, message, packetName, metadata);
  }

  sendLocalBoundSessionResponse(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean
  ): boolean {
    return this.boundSessions.sendLocalBoundSessionResponse(
      actorId,
      packetName,
      requestSeq,
      message,
      metadata,
      compressPayload
    );
  }

  sendLocalBoundSessionError(
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>
  ): boolean {
    return this.boundSessions.sendLocalBoundSessionError(actorId, packetName, requestSeq, error, metadata);
  }


  async sendNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendNativeBoundSession(node, actorRef, message, packetName, metadata, signal);
  }

  async sendNativeBoundSessionResponse(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    metadata: ReadonlyMap<string, string>,
    compressPayload: boolean,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendNativeBoundSessionResponse(
      node,
      actorRef,
      packetName,
      requestSeq,
      message,
      metadata,
      compressPayload,
      signal
    );
  }

  async sendNativeBoundSessionError(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.sendNativeBoundSessionError(node, actorRef, packetName, requestSeq, error, metadata, signal);
  }

  async disconnectNativeBoundSession(
    node: ZLinkBackendSpotNode,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundSessions.disconnectNativeBoundSession(node, actorRef, signal);
  }

  async disconnectBoundSession(actorId: string, signal?: AbortSignal): Promise<void> {
    await this.boundSessions.disconnectBoundSession(actorId, signal);
  }

  async relay(
    actor: DefaultZLinkSessionActor,
    payload: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<void> {
    await this.boundActorRelay.relay(actor, payload, signal);
  }

  async notifyDisconnected(actor: DefaultZLinkSessionActor, signal?: AbortSignal): Promise<void> {
    await this.boundActorRelay.notifyDisconnected(actor, signal);
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

  createJsonReplyFrameMessage(
    requestHeader: ZLinkStreamFrameHeader,
    kind: ZLinkStreamReplyMessageKind,
    metadata: ReadonlyMap<string, string>,
    compressed: boolean,
    payload: unknown
  ): Message {
    return this.frameMessages.createJsonReplyFrameMessage(requestHeader, kind, metadata, compressed, payload);
  }

}
