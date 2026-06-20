import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendSpotNode
} from '../backend';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ActorRef,
  Message,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkBoundSession,
  ZLinkBoundSessionSendCall,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotRemoteAddressResolver,
  ZlinkStreamHeader
} from '../../contracts';
import { ZLinkSpotKind } from '../../contracts';
import {
  DefaultZLinkChannelClient,
  DefaultZLinkFanoutClient,
  DefaultZLinkSpotPublisherClient,
  ZLinkDispatchErrorReporter,
  ZLinkChannelRuntimeManager,
  ZLinkRuntimeChannelTransport,
  ZLinkRuntimeRouteTransport
} from '../channels';
import { ZLinkFrameworkRuntimeState, ZLinkRuntimeErrorSink } from '../execution';
import { DefaultZLinkSpotManager, ZLinkRuntimeSpotPublisherTransport, ZLinkSpotNodeRuntimeManager } from '../spots';
import type {
  DefaultZLinkActorManager,
  ZLinkActorJoinCoordinator,
  ZLinkActorManagerOptions,
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget,
  ZLinkActorRuntimeState,
  ZLinkActorRoutedJoinTransport
} from '../actors';
import {
  ZLinkActorNativeJoinCoordinator,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import {
  encodeStreamHeader,
  messageToBytes,
  requireStreamFrameHeader,
  ZLinkStreamMessageKind
} from '../streams/protocol';

export interface ZLinkFrameworkRuntime {
  readonly isStarted: boolean;
  start(): Promise<void>;
  stop(): Promise<void>;
}

export interface ZLinkFrameworkRuntimeHostOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly lifecycleSink?: string[];
  readonly providerResolver?: ZLinkProviderResolver;
}

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkRuntimeState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  private readonly preStartErrorSink = new ZLinkRuntimeErrorSink();
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(() => this.channelRuntime);
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.streamBindingRuntime = new ZLinkStreamBindingRuntime({
      streamPayloadCodec: resolveStreamPayloadCodec(options.registration),
      relay: (actor, header, payload, signal) =>
        this.relayRemoteActorPacket(actor.actorId, header, payload, signal)
    });
    this.boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);
  }

  get isStarted(): boolean {
    return this.state !== undefined;
  }

  get context(): ZLinkBackendContext | undefined {
    return this.state?.context as ZLinkBackendContext | undefined;
  }

  get taskRunner(): ZLinkFrameworkRuntimeState['taskRunner'] | undefined {
    return this.state?.taskRunner;
  }

  get errorSink(): ZLinkFrameworkRuntimeState['errorSink'] | undefined {
    return this.state?.errorSink;
  }

  async start(): Promise<void> {
    if (this.state !== undefined) {
      return;
    }

    this.lifecycleSink?.push('framework:start');
    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    const context = channelAdapter.createContext();
    let channelRuntime: ZLinkChannelRuntimeManager | undefined;
    let spotNodeRuntime: ZLinkSpotNodeRuntimeManager | undefined;
    let streamRuntime: ZLinkStreamRuntimeManager | undefined;
    try {
      this.state = new ZLinkFrameworkRuntimeState(context);
      const dispatchErrors = new ZLinkDispatchErrorReporter(
        this.options.registration.dispatch?.messageDispatchErrorObserverType,
        this.options.providerResolver,
        this.state.errorSink
      );
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver
      );
      this.channelRuntime = channelRuntime;
      spotNodeRuntime = new ZLinkSpotNodeRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        channelClient: new DefaultZLinkChannelClient(this.options.registration, this.channelTransport),
        fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.channelTransport),
        spotPublisherClient: new DefaultZLinkSpotPublisherClient(this.options.registration, this.spotPublisherTransport),
        providerResolver: this.options.providerResolver,
        dispatchErrors,
        actorResolver: (actorId) => this.actorManager?.getState(actorId)?.actor,
        entryActorCommitter: (actor) => {
          const state = this.actorManager?.getState(actor.actorId);
          const entryNode = this.spotNodeRuntime?.primaryNode;
          if (state === undefined || entryNode === undefined) {
            return;
          }
          const generation = state.nativeActorRef?.generation ?? 0n;
          state.clearJoinedSpot();
          state.setNativeActorRef({
            nodeRid: entryNode.routingId,
            actorId: actor.actorId,
            generation
          } as ZLinkBackendActorRef);
        },
        routedBoundSessionReceiver: (actorId, message, packetName, metadata) =>
          this.receiveRoutedBoundSession(actorId, message, packetName, metadata),
        actorResponseSender: (actor, packetName, requestSeq, response, metadata, signal) =>
          this.sendActorResponse(actor, packetName, requestSeq, response, metadata, signal),
        actorDestroyer: (node, entryNodeRid, actor, signal) => {
          if (this.actorManager === undefined) {
            throw new Error('Entry Spot actor destroy requires ZLINK_ACTOR_MANAGER.');
          }
          return this.actorManager.destroyActor(node, entryNodeRid, actor, signal);
        }
      });
      await spotNodeRuntime.start();
      channelRuntime.setSpotNodes(spotNodeRuntime.nodesByName);
      this.state.listenerTasks.push(...channelRuntime.start(this.state.taskRunner));
      this.spotNodeRuntime = spotNodeRuntime;
      streamRuntime = new ZLinkStreamRuntimeManager({
        registration: this.options.registration,
        backendAdapterFactory: this.backendAdapterFactory,
        context,
        bindingRuntime: this.streamBindingRuntime,
        spotNodes: spotNodeRuntime.nodesByName,
        providerResolver: this.options.providerResolver
      });
      streamRuntime.start();
      this.streamRuntime = streamRuntime;
      this.lifecycleSink?.push('framework:started');
    } catch (error) {
      await Promise.allSettled([
        streamRuntime?.dispose(),
        spotNodeRuntime?.dispose(),
        channelRuntime?.dispose(),
        context.dispose()
      ]);
      throw error;
    }
  }

  async stop(): Promise<void> {
    const state = this.state;
    if (state === undefined) {
      return;
    }

    const channelRuntime = this.channelRuntime;
    const spotNodeRuntime = this.spotNodeRuntime;
    const streamRuntime = this.streamRuntime;
    this.state = undefined;
    this.channelRuntime = undefined;
    this.spotNodeRuntime = undefined;
    this.streamRuntime = undefined;
    this.lifecycleSink?.push('framework:stop');
    await streamRuntime?.dispose();
    await spotNodeRuntime?.dispose();
    await channelRuntime?.dispose();
    await state.dispose();
    this.lifecycleSink?.push('framework:stopped');
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    await this.stop();
  }

  setActorManager(actorManager: DefaultZLinkActorManager): void {
    this.actorManager = actorManager;
  }

  setSpotManager(spotManager: DefaultZLinkSpotManager): void {
    this.spotManager = spotManager;
  }

  createRegistrySpotRemoteAddressResolver(): ZLinkSpotRemoteAddressResolver {
    return {
      resolve: async (spotRid: RoutingId) => this.requireSpotNodeRuntime()
        .resolveRegistrySpotRemoteAddress(spotRid)
    };
  }

  createActorManagerOptions(remoteAddressResolver?: ZLinkSpotRemoteAddressResolver): Pick<
    ZLinkActorManagerOptions,
    | 'joinCoordinator'
    | 'messageSerializers'
    | 'nativeActorNode'
    | 'nativeActorNodeProvider'
    | 'actorCreatedNodeRidProvider'
    | 'actorCreatedNotifier'
    | 'actorDestroyedCleanup'
    | 'boundSessionFactory'
  > {
    return {
      joinCoordinator: new ZLinkLocalFirstActorJoinCoordinator({
        localSpotManager: () => this.spotManager,
        nativeNode: () => this.requirePrimarySpotNode(),
        native: new ZLinkLazyNativeJoinCoordinator(
          () => this.requirePrimarySpotNode(),
          remoteAddressResolver,
          this.routeTransport,
          (actorRef, signal, force) => force === true
            ? this.streamBindingRuntime.refreshActor(actorRef, signal)
            : this.streamBindingRuntime.rebindActor(actorRef, signal)
        )
      }),
      messageSerializers: this.options.registration.messageSerializers,
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      boundSessionFactory: (actorId) => new ZLinkNativeFallbackBoundSession(
        this.streamBindingRuntime,
        this.routeTransport,
        () => this.requirePrimarySpotNode(),
        () => this.actorManager?.getState(actorId)?.nativeActorRef as ActorRef | undefined,
        () => this.actorManager?.getState(actorId)?.remoteBoundSessionTarget,
        this.options.registration.requestTimeoutMs,
        actorId
      ),
      actorCreatedNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      actorCreatedNotifier: (nodeRid, actor, signal) =>
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, signal) ?? Promise.resolve(),
      actorDestroyedCleanup: (actorId) => this.streamBindingRuntime.unbindActor(actorId)
    };
  }

  createSpotManagerOptions(): object {
    return {
      nodeRid: undefined,
      nodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      entryNodeRid: undefined,
      entryNodeRidProvider: () => this.spotNodeRuntime?.primaryNode?.routingId,
      actorEntryNodeRidProvider: (actor: ZLinkActor) =>
        this.actorManager?.getState(actor.actorId)?.nativeActorRef?.nodeRid as RoutingId | undefined,
      entrySpotCallbacks: {
        onJoinedActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorJoined(actor, signal) ?? Promise.resolve(),
        onLeaveActor: (actor: ZLinkActor, signal?: AbortSignal) =>
          this.spotNodeRuntime?.notifyPrimaryEntrySpotActorLeft(actor, signal) ?? Promise.resolve()
      },
      channelClient: new DefaultZLinkChannelClient(this.options.registration, this.channelTransport),
      fanoutClient: new DefaultZLinkFanoutClient(this.options.registration, this.channelTransport),
      spotPublisherClient: new DefaultZLinkSpotPublisherClient(this.options.registration, this.spotPublisherTransport),
      messageSerializers: this.options.registration.messageSerializers,
      createNativeSpot: (spotRid: RoutingId) => this.spotNodeRuntime?.primaryNode?.getOrCreateSpot(spotRid).spot,
      actorResolver: (actorId: string) => this.actorManager?.getState(actorId)?.actor,
      routedActorProvider: async (
        actorId: string,
        actorType: string,
        actorRef?: ActorRef,
        remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
        signal?: AbortSignal
      ) => {
        if (this.actorManager === undefined) {
          throw new Error('Routed actor join requires ZLINK_ACTOR_MANAGER.');
        }
        const actor = await this.actorManager.getOrCreate(actorId, actorType, signal);
        const state = this.actorManager.getState(actorId);
        if (state === undefined) {
          throw new Error(`Actor '${actorId}' state was not created.`);
        }
        if (actorRef !== undefined) {
          state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
          state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
          return { actor, actorRef: actorRef as unknown as ZLinkBackendActorRef };
        }
        state.setRemoteBoundSessionTarget(undefined);
        const localActorRef = state.ensureNativeActorRef(this.requirePrimarySpotNode());
        return { actor, actorRef: localActorRef };
      },
      routedActorCommitter: (actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot) => {
        this.actorManager?.getState(actor.actorId)?.setJoinedSpot(spotRid, spot);
      },
      routedBoundSessionReceiver: async (
        actorId: string,
        message: unknown,
        packetName: string | undefined,
        metadata: ReadonlyMap<string, string>
      ) => {
        await this.receiveRoutedBoundSession(actorId, message, packetName, metadata);
      },
      actorResponseSender: async (
        actor: ZLinkActor,
        packetName: string,
        requestSeq: bigint,
        response: unknown,
        metadata: ReadonlyMap<string, string>,
        signal?: AbortSignal
      ) => this.sendActorResponse(actor, packetName, requestSeq, response, metadata, signal),
      dispatchErrors: new ZLinkDispatchErrorReporter(
        this.options.registration.dispatch?.messageDispatchErrorObserverType,
        this.options.providerResolver,
        {
          reportRuntimeTaskException: (taskName, error) =>
            (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException(taskName, error)
        }
      )
    };
  }

  requirePrimarySpotNode(): ZLinkBackendSpotNode {
    const node = this.spotNodeRuntime?.primaryNode;
    if (node === undefined) {
      throw new Error('Primary Entry Spot node is not started.');
    }
    return node;
  }

  private requireSpotNodeRuntime(): ZLinkSpotNodeRuntimeManager {
    const runtime = this.spotNodeRuntime;
    if (runtime === undefined) {
      throw new Error('SPOT node runtime is not started.');
    }
    return runtime;
  }

  private async receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): Promise<void> {
    const sent = this.streamBindingRuntime.sendLocalBoundSession(actorId, message, packetName, metadata);
    if (!sent) {
      throw new Error(`Actor '${actorId}' local bound session route is not ready.`);
    }
  }

  private async sendActorResponse(
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ): Promise<void> {
    const state = this.actorManager?.getState(actor.actorId);
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' does not have a native actor ref.`);
    }
    if (process.env.ZLINK_DEBUG_ACTOR_RESPONSE === '1') {
      console.log(`actor-response send actor=${actor.actorId} packet=${packetName} seq=${requestSeq} node=${String(actorRef.nodeRid)}`);
    }
    if (this.streamBindingRuntime.sendLocalBoundSessionResponse(
      actor.actorId,
      packetName,
      requestSeq,
      response,
      metadata
    )) {
      if (process.env.ZLINK_DEBUG_ACTOR_RESPONSE === '1') {
        console.log(`actor-response local actor=${actor.actorId} seq=${requestSeq}`);
      }
      return;
    }
    if (process.env.ZLINK_DEBUG_ACTOR_RESPONSE === '1') {
      console.log(`actor-response native actor=${actor.actorId} seq=${requestSeq}`);
    }
    await this.streamBindingRuntime.sendNativeBoundSessionResponse(
      this.requirePrimarySpotNode(),
      actorRef,
      packetName,
      requestSeq,
      response,
      metadata,
      signal
    );
  }

  private async relayRemoteActorPacket(
    actorId: string,
    header: ZlinkStreamHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    const remoteTarget = this.actorManager?.getState(actorId)?.remoteActorPacketTarget;
    if (remoteTarget === undefined) {
      return false;
    }
    const frameHeader = requireStreamFrameHeader(header);
    const remoteAddress = remoteActorPacketRemoteAddress(remoteTarget);
    const request = BindingMessage.from(Buffer.from(JSON.stringify({
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId,
      header: Buffer.from(encodeStreamHeader(frameHeader)).toString('base64'),
      payload: Buffer.from(messageToBytes(payload)).toString('base64')
    })));
    try {
      const replyParts = await this.routeTransport.requestRawToSpot(
        remoteAddress,
        request,
        { timeoutMs: this.options.registration.requestTimeoutMs, signal }
      );
      try {
        if (
          frameHeader.requestSeq !== undefined &&
          replyParts.length > 0
        ) {
          const rawReply = replyParts[0].data().toString();
          const reply = JSON.parse(rawReply) as {
            readonly ok?: boolean;
            readonly error?: unknown;
            readonly response?: unknown;
          };
          if (reply.ok === false) {
            const sent = this.streamBindingRuntime.sendLocalBoundSessionError(
              actorId,
              frameHeader.name,
              frameHeader.requestSeq,
              reply.error ?? 'Remote actor request failed.',
              streamMetadataMap(frameHeader.metadata)
            );
            if (!sent) {
              throw new Error(`Actor '${actorId}' local bound session error response route is not ready.`);
            }
            return true;
          }
          const sent = this.streamBindingRuntime.sendLocalBoundSessionResponse(
            actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            reply.response,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw new Error(`Actor '${actorId}' local bound session response route is not ready.`);
          }
        }
      } finally {
        for (const part of replyParts) {
          part.close();
        }
      }
    } finally {
      request.close();
    }
    return true;
  }
}

function remoteActorPacketRemoteAddress(target: ZLinkRemoteActorPacketTarget) {
  return {
    routerChannelId: target.routerChannelId,
    targetNodeRid: target.targetNodeRid,
    spotRid: target.spotRid,
    spotKind: ZLinkSpotKind.Entry
  };
}

function streamMetadataMap(metadata: unknown): ReadonlyMap<string, string> {
  if (metadata instanceof Map) {
    return new Map(metadata);
  }
  const maybeValues = metadata as { values?: unknown } | undefined;
  if (maybeValues?.values instanceof Map) {
    return new Map(maybeValues.values);
  }
  return new Map();
}

class ZLinkNativeFallbackBoundSession implements ZLinkBoundSession {
  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly routedTransport: ZLinkActorRoutedJoinTransport,
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly actorRefProvider: () => ActorRef | undefined,
    private readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined,
    private readonly requestTimeoutMs: number | undefined,
    private readonly actorId: string
  ) {}

  send(message: unknown): ZLinkBoundSessionSendCall {
    return new ZLinkNativeFallbackBoundSessionSendCall(
      this.runtime,
      this.routedTransport,
      this.nodeProvider,
      this.actorRefProvider,
      this.remoteBoundSessionTargetProvider,
      this.requestTimeoutMs,
      this.actorId,
      message
    );
  }

  async disconnect(signal?: AbortSignal): Promise<void> {
    const actorRef = this.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.runtime.disconnectNativeBoundSession(this.nodeProvider(), actorRef, signal);
      return;
    }
    await this.runtime.disconnectBoundSession(this.actorId, signal);
  }
}

class ZLinkNativeFallbackBoundSessionSendCall implements ZLinkBoundSessionSendCall {
  private selectedPacketName: string | undefined;
  private readonly selectedMetadata = new Map<string, string>();
  private executed = false;

  constructor(
    private readonly runtime: ZLinkStreamBindingRuntime,
    private readonly routedTransport: ZLinkActorRoutedJoinTransport,
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly actorRefProvider: () => ActorRef | undefined,
    private readonly remoteBoundSessionTargetProvider: () => ZLinkRemoteBoundSessionTarget | undefined,
    private readonly requestTimeoutMs: number | undefined,
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

  async submit(signal?: AbortSignal): Promise<void> {
    if (this.executed) {
      throw new Error('Bound session send already submitted.');
    }
    this.executed = true;
    if (this.runtime.sendLocalBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata
    )) {
      return;
    }
    const remoteTarget = this.remoteBoundSessionTargetProvider();
    if (remoteTarget !== undefined) {
      const remoteAddress = {
        routerChannelId: remoteTarget.routerChannelId,
        targetNodeRid: remoteTarget.targetNodeRid,
        spotRid: remoteTarget.spotRid,
        spotKind: ZLinkSpotKind.Entry
      };
      const envelope = {
        actorId: this.actorId,
        message: this.message,
        packetName: this.selectedPacketName,
        metadata: Object.fromEntries(this.selectedMetadata)
      };
      if (this.routedTransport.requestRawToSpot !== undefined) {
        const request = BindingMessage.from(Buffer.from(JSON.stringify({
          packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
          actorId: envelope.actorId,
          message: envelope.message,
          boundPacketName: envelope.packetName,
          metadata: envelope.metadata
        })));
        try {
          const replyParts = await this.routedTransport.requestRawToSpot(
            remoteAddress,
            request,
            { timeoutMs: this.requestTimeoutMs, signal }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          request.close();
        }
      } else if (this.routedTransport.requestRawFromSpotToSpot !== undefined) {
        const sourceSpot = this.nodeProvider()
          .getOrCreateSpot(`__zlink.actor.bound.${String(remoteTarget.targetNodeRid)}`)
          .spot;
        const request = BindingMessage.from(Buffer.from(JSON.stringify({
          packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
          actorId: envelope.actorId,
          message: envelope.message,
          boundPacketName: envelope.packetName,
          metadata: envelope.metadata
        })));
        try {
          const replyParts = await this.routedTransport.requestRawFromSpotToSpot(
            sourceSpot,
            remoteAddress,
            request,
            { timeoutMs: this.requestTimeoutMs, signal }
          );
          for (const part of replyParts) {
            part.close();
          }
        } finally {
          request.close();
        }
      } else if (this.routedTransport.requestFromSpotToSpot !== undefined) {
        const sourceSpot = this.nodeProvider()
          .getOrCreateSpot(`__zlink.actor.bound.${String(remoteTarget.targetNodeRid)}`)
          .spot;
        await this.routedTransport.requestFromSpotToSpot(
          sourceSpot,
          remoteAddress,
          envelope,
          { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, timeoutMs: this.requestTimeoutMs, signal }
        );
      } else {
        await this.routedTransport.requestToSpot(
          remoteAddress,
          envelope,
          { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, timeoutMs: this.requestTimeoutMs, signal }
        );
      }
      return;
    }
    const actorRef = this.actorRefProvider();
    if (actorRef !== undefined) {
      await nextNativeGatewayTurn();
      await this.runtime.sendNativeBoundSession(
        this.nodeProvider(),
        actorRef,
        this.message,
        this.selectedPacketName,
        this.selectedMetadata,
        signal
      );
      return;
    }
    await this.runtime.sendBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata,
      signal
    );
  }
}

function nextNativeGatewayTurn(): Promise<void> {
  return new Promise((resolve) => setImmediate(resolve));
}

interface ZLinkLocalFirstActorJoinCoordinatorOptions {
  readonly localSpotManager: () => DefaultZLinkSpotManager | undefined;
  readonly nativeNode: () => ZLinkBackendSpotNode;
  readonly native: ZLinkActorJoinCoordinator;
}

class ZLinkLocalFirstActorJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(private readonly options: ZLinkLocalFirstActorJoinCoordinatorOptions) {}

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const localSpotManager = this.options.localSpotManager();
    if (
      state.nativeActorRef !== undefined ||
      localSpotManager === undefined ||
      !localSpotManager.hasActiveSpot(spotRid)
    ) {
      return this.options.native.joinSpot(actor, state, spotRid, request, timeoutMs, signal);
    }
    const result = await localSpotManager.admitActorJoin(
      spotRid,
      actor,
      request,
      (spot: ZLinkSpot) => state.setJoinedSpot(spotRid, spot),
      signal
    );
    return {
      resultCode: result.accepted ? 0 : 1,
      actor: localActorRef(nodeRidForLocalActor(this.options.nativeNode), actor.actorId),
      reply: result.reply
    };
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    // Entry Spot join always goes through the native round-trip. The target
    // node's runtime drains the admission request from `recvActorJoin`, runs
    // `onActorJoin`, and replies; core commits membership and routes the reply
    // back here. This is identical for a local or remote target node, so the
    // admission behaviour does not diverge by caller locality.
    return this.options.native.joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}

class ZLinkLazyNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(
    private readonly nodeProvider: () => ZLinkBackendSpotNode,
    private readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver,
    private readonly routedTransport?: ZLinkActorRoutedJoinTransport,
    private readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>
  ) {}

  joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    return new ZLinkActorNativeJoinCoordinator({
      node: this.nodeProvider(),
      remoteAddressResolver: this.remoteAddressResolver,
      routedTransport: this.routedTransport,
      remoteActorBinder: this.remoteActorBinder
    })
      .joinSpot(actor, state, spotRid, request, timeoutMs, signal);
  }

  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    return new ZLinkActorNativeJoinCoordinator({
      node: this.nodeProvider(),
      remoteAddressResolver: this.remoteAddressResolver,
      routedTransport: this.routedTransport,
      remoteActorBinder: this.remoteActorBinder
    })
      .joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

function resolveBackendAdapterFactory(internalOptions: unknown): ZLinkBackendAdapterFactory {
  if (
    typeof internalOptions === 'object'
    && internalOptions !== null
    && 'backendAdapterFactory' in internalOptions
  ) {
    const factory = (internalOptions as { readonly backendAdapterFactory?: ZLinkBackendAdapterFactory }).backendAdapterFactory;
    if (factory !== undefined) {
      return factory;
    }
  }
  return new ZLinkNodeBackendAdapterFactory();
}

function resolveStreamPayloadCodec(registration: ZLinkFrameworkRegistration): ZLinkStreamPayloadCodec | undefined {
  const codec = registration.codecs.streamCodecs.values().next().value;
  if (isStreamPayloadCodec(codec)) {
    return codec;
  }
  return undefined;
}

function isStreamPayloadCodec(codec: unknown): codec is ZLinkStreamPayloadCodec {
  return typeof codec === 'object'
    && codec !== null
    && typeof (codec as { encode?: unknown }).encode === 'function';
}
