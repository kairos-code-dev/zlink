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
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkBoundSession,
  ZLinkBoundSessionSendCall,
  ZLinkProviderResolver,
  ZLinkRouteRequestContext,
  ZLinkSessionActor,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotRemoteAddressResolver
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import { ZLinkMessageFlowLogMode, ZLinkSpotKind } from '../../contracts';
import type { ZLinkMessageFlowControl } from '../../contracts';
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
import {
  createDiagnosticsContext,
  type ZLinkMessageFlowModeCell
} from '../diagnostics';
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
  ZLINK_REMOTE_ACTOR_JOIN_PACKET,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from '../actors';
import {
  DefaultZLinkBoundSessionFactory,
  type ZLinkStreamPayloadCodec,
  ZLinkStreamBindingRuntime,
  ZLinkStreamRuntimeManager
} from '../streams';
import { Message as BindingMessage, RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  messageToBytes,
  type ZLinkStreamFrameHeader,
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

export class ZLinkFrameworkRuntimeHost implements ZLinkFrameworkRuntime, ZLinkMessageFlowControl {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly lifecycleSink?: string[];
  private state?: ZLinkFrameworkRuntimeState;
  private channelRuntime?: ZLinkChannelRuntimeManager;
  private spotNodeRuntime?: ZLinkSpotNodeRuntimeManager;
  private streamRuntime?: ZLinkStreamRuntimeManager;
  private actorManager?: DefaultZLinkActorManager;
  private spotManager?: DefaultZLinkSpotManager;
  // Shared, runtime-mutable message-flow mode cell — installed once so
  // setMessageFlowMode flips every surface live. Seeded from config at start().
  private readonly messageFlowModeCell: ZLinkMessageFlowModeCell = {
    mode: ZLinkMessageFlowLogMode.ErrorsOnly
  };
  private readonly preStartErrorSink = new ZLinkRuntimeErrorSink();
  readonly channelTransport = new ZLinkRuntimeChannelTransport(() => this.channelRuntime);
  readonly routeTransport = new ZLinkRuntimeRouteTransport(
    () => this.channelRuntime,
    (routerChannelId) => this.options.registration.routeChannels.has(routerChannelId)
  );
  readonly spotPublisherTransport = new ZLinkRuntimeSpotPublisherTransport(() => this.spotNodeRuntime);
  readonly streamBindingRuntime: ZLinkStreamBindingRuntime;
  readonly boundSessionFactory: DefaultZLinkBoundSessionFactory;
  private readonly sessionActorPacketTargets = new WeakMap<ZLinkSessionActor, ZLinkRemoteActorPacketTarget>();
  private readonly sessionActorPacketTargetsByActor = new Map<string, ZLinkRemoteActorPacketTarget>();

  constructor(readonly options: ZLinkFrameworkRuntimeHostOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.lifecycleSink = options.lifecycleSink;
    this.streamBindingRuntime = new ZLinkStreamBindingRuntime({
      streamPayloadCodec: resolveStreamPayloadCodec(options.registration),
      messageSerializers: options.registration.messageSerializers,
      nativeActorNodeProvider: () => this.spotNodeRuntime?.primaryNode,
      relay: (actor, header, payload, signal) =>
        this.relayRemoteActorPacket(actor, header, payload, signal)
    });
    this.boundSessionFactory = new DefaultZLinkBoundSessionFactory(this.streamBindingRuntime);
  }

  get isStarted(): boolean {
    return this.state !== undefined;
  }

  /**
   * Runtime toggle (ZLinkMessageFlowControl): flip the shared live-mode cell so every
   * surface starts/stops tracing without a restart.
   */
  setMessageFlowMode(mode: ZLinkMessageFlowLogMode): void {
    this.messageFlowModeCell.mode = mode;
  }

  messageFlowMode(): ZLinkMessageFlowLogMode {
    return this.messageFlowModeCell.mode;
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
      // Seed the shared live-mode cell from the configured mode (default errorsOnly).
      this.messageFlowModeCell.mode =
        this.options.registration.dispatch?.diagnostics?.messageFlowLogMode ??
        ZLinkMessageFlowLogMode.ErrorsOnly;
      const dispatchErrors = new ZLinkDispatchErrorReporter(
        undefined,
        undefined,
        this.state.errorSink,
        createDiagnosticsContext(
          this.options.registration.dispatch,
          this.options.providerResolver,
          this.messageFlowModeCell
        )
      );
      channelRuntime = new ZLinkChannelRuntimeManager(
        this.options.registration,
        channelAdapter,
        context,
        this.options.providerResolver,
        {
          messageFlowModeCell: this.messageFlowModeCell,
          internalRouteSendHandlers: new Map([
            [ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, {
              handle: async (payload) => {
                await this.receiveRemoteBoundSessionSend(payload);
              }
            }]
          ]),
          internalRouteRequestHandlers: new Map([
            [ZLINK_REMOTE_ACTOR_JOIN_PACKET, {
              handle: (payload, routeContext) =>
                this.receiveRemoteActorJoin(payload, routeContext)
            }],
            [ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET, {
              handle: (payload, routeContext) =>
                this.receiveRemoteActorPacketRelay(payload, routeContext)
            }]
          ])
        }
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
        messageSerializers: this.options.registration.messageSerializers,
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
        remoteActorPacketTargetReceiver: (actorId, target) => {
          const state = this.actorManager?.getState(actorId);
          state?.setRemoteBoundSessionTarget(target);
        },
        actorPacketTargetProvider: (actorId) => this.actorPacketTargetForState(actorId),
        localActorPacketRouter: async (actorId, parts, returnResponse, remoteBoundSessionTarget) => {
          const spotRid = this.actorManager?.getState(actorId)?.spotRid;
          if (spotRid === undefined || this.spotManager === undefined) {
            return { handled: false };
          }
          return {
            handled: true,
            response: await this.spotManager.dispatchRoutedActorPacket(
              spotRid,
              actorId,
              parts,
              returnResponse,
              remoteBoundSessionTarget
            )
          };
        },
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
        providerResolver: this.options.providerResolver,
        dispatchErrors
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
    state.abortController.abort();
    await Promise.allSettled(state.listenerTasks);
    await new Promise<void>((resolve) => setImmediate(resolve));
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
      actorCreatedNotifier: (nodeRid, actor, createRequest, signal) =>
        this.spotNodeRuntime?.notifyEntrySpotActorCreated(nodeRid, actor, createRequest, signal) ?? Promise.resolve(),
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
      nativeSpotNodeProvider: () => this.spotNodeRuntime?.primaryNode,
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
        const actor = actorRef === undefined
          ? await this.actorManager.getOrCreateActor(actorId, actorType, signal)
          : await this.actorManager.getOrCreateWithNativeRef(
              actorId,
              actorType,
              actorRef as unknown as ZLinkBackendActorRef,
              signal
            );
        const state = this.actorManager.getState(actorId);
        if (state === undefined) {
          throw new Error(`Actor '${actorId}' state was not created.`);
        }
        if (actorRef !== undefined) {
          state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
          state.setRemoteBoundSessionTarget(remoteBoundSessionTarget);
          return { actor, actorRef: actorRef as unknown as ZLinkBackendActorRef };
        }
        const localActorRef = state.ensureNativeActorRef(this.requirePrimarySpotNode());
        return { actor, actorRef: localActorRef };
      },
      nativeJoinBoundSessionTargetResolver: (info: { sourceActor: ActorRef; sourceSpotRid?: RoutingId }) => {
        const routerChannelId = this.options.registration.registrySpotRemoteAddresses?.routerChannelId
          ?? this.firstAcceptedSpotRouteChannel();
        if (routerChannelId === undefined) {
          return undefined;
        }
        return {
          routerChannelId,
          targetNodeRid: normalizeRuntimeRoutingId(info.sourceActor.nodeRid),
          spotRid: normalizeRuntimeRoutingId(info.sourceSpotRid ?? info.sourceActor.nodeRid)
        };
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
      remoteActorPacketTargetReceiver: (actorId: string, target: ZLinkRemoteBoundSessionTarget) => {
        const state = this.actorManager?.getState(actorId);
        state?.setRemoteBoundSessionTarget(target);
      },
      actorPacketTargetProvider: (actorId: string) => this.actorPacketTargetForState(actorId),
      actorResponseSender: async (
        actor: ZLinkActor,
        packetName: string,
        requestSeq: bigint,
        response: unknown,
        metadata: ReadonlyMap<string, string>,
        signal?: AbortSignal
      ) => this.sendActorResponse(actor, packetName, requestSeq, response, metadata, signal),
      dispatchErrors: new ZLinkDispatchErrorReporter(
        undefined,
        undefined,
        {
          reportRuntimeTaskException: (taskName: string, error: unknown) =>
            (this.errorSink ?? this.preStartErrorSink).reportRuntimeTaskException(taskName, error)
        },
        createDiagnosticsContext(
          this.options.registration.dispatch,
          this.options.providerResolver,
          this.messageFlowModeCell
        )
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

  private requireSpotManager(): DefaultZLinkSpotManager {
    const manager = this.spotManager;
    if (manager === undefined) {
      throw new Error('SPOT manager runtime is not started.');
    }
    return manager;
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const manager = this.actorManager;
    if (manager === undefined) {
      throw new Error('Actor manager runtime is not started.');
    }
    return manager;
  }

  private async receiveRoutedBoundSession(
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>
  ): Promise<void> {
    const sent = this.streamBindingRuntime.sendLocalBoundSession(actorId, message, packetName, metadata);
    if (sent) {
      return;
    }
    const boundSessionFactory = this.createActorManagerOptions().boundSessionFactory;
    if (boundSessionFactory === undefined) {
      throw new Error('Bound session factory is not configured.');
    }
    const call = boundSessionFactory(actorId).send(message);
    if (packetName !== undefined) {
      call.packetName(packetName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    await call.submit();
  }

  private async receiveRemoteBoundSessionSend(payload: unknown): Promise<{ readonly ok: boolean }> {
    const send = decodeRemoteBoundSessionSendPayload(payload);
    const metadata = new Map(Object.entries(send.metadata ?? {}));
    if (this.streamBindingRuntime.sendLocalBoundSession(
      send.actorId,
      send.message,
      send.boundPacketName,
      metadata
    )) {
      return { ok: true };
    }
    const boundSessionFactory = this.createActorManagerOptions().boundSessionFactory;
    if (boundSessionFactory === undefined) {
      throw new Error('Bound session factory is not configured.');
    }
    const call = boundSessionFactory(send.actorId).send(send.message);
    if (send.boundPacketName !== undefined) {
      call.packetName(send.boundPacketName);
    }
    for (const [key, value] of metadata) {
      call.metadata(key, value);
    }
    await call.submit();
    return { ok: true };
  }

  private async receiveRemoteActorJoin(
    payload: unknown,
    routeContext: ZLinkRouteRequestContext
  ): Promise<{
    readonly accepted: boolean;
    readonly actorNodeRid: string;
    readonly actorNodeRidHex?: string;
    readonly actorId: string;
    readonly actorGeneration: string;
    readonly reply?: string;
  }> {
    const join = decodeRemoteActorJoinPayload(payload);
    const actorManager = this.requireActorManager();
    const actor = await actorManager.getOrCreateActor(join.actorId, join.actorType);
    const state = actorManager.getState(join.actorId);
    if (state === undefined) {
      throw new Error(`Actor '${join.actorId}' state was not created.`);
    }
    const actorRef = {
      nodeRid: normalizeRuntimeRoutingId(join.actorNodeRid),
      actorId: join.actorId,
      generation: BigInt(join.actorGeneration)
    };
    state.setNativeActorRef(actorRef as unknown as ZLinkBackendActorRef);
    state.setRemoteBoundSessionTarget({
      routerChannelId: join.boundSessionRouterChannelId ?? join.routerChannelId ?? routeContext.channelName ?? '',
      targetNodeRid: normalizeRuntimeRoutingId(join.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
      spotRid: normalizeRuntimeRoutingId(join.boundSessionSpotRid ?? join.sourceSpotRid ?? routeContext.sourceNodeRid)
    });
    const request = BindingMessage.from(Buffer.from(join.request, 'base64'));
    try {
      const response = await this.requireSpotManager().admitActorJoin(
        join.spotRid as RoutingId,
        actor,
        request,
        (spot) => state.setJoinedSpot(join.spotRid as RoutingId, spot)
      );
      const reply = response.reply as Message | undefined;
      return {
        accepted: response.accepted,
        actorNodeRid: join.actorNodeRid,
        actorNodeRidHex: join.actorNodeRidHex,
        actorId: join.actorId,
        actorGeneration: join.actorGeneration,
        reply: reply?.data().toString('base64')
      };
    } finally {
      request.close();
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
    if (this.streamBindingRuntime.sendLocalBoundSessionResponse(
      actor.actorId,
      packetName,
      requestSeq,
      response,
      metadata
    )) {
      return;
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

  private async receiveRemoteActorPacketRelay(
    payload: unknown,
    routeContext: ZLinkRouteRequestContext
  ): Promise<{
    readonly ok: boolean;
    readonly error?: unknown;
    readonly response?: unknown;
    readonly actorPacketTarget?: unknown;
  }> {
    const relay = decodeRemoteActorPacketRelayPayload(payload);
    const remoteBoundSessionTarget: ZLinkRemoteBoundSessionTarget | undefined =
      relay.routerChannelId === undefined
        ? undefined
        : {
            routerChannelId: relay.routerChannelId,
            targetNodeRid: normalizeRuntimeRoutingId(relay.boundSessionTargetNodeRid ?? routeContext.sourceNodeRid),
            spotRid: normalizeRuntimeRoutingId(relay.boundSessionSpotRid ?? routeContext.sourceNodeRid)
          };
    const header = BindingMessage.from(Buffer.from(relay.header, 'base64'));
    const body = BindingMessage.from(Buffer.from(relay.payload, 'base64'));
    try {
      const state = this.actorManager?.getState(relay.actorId);
      const response = state?.spotRid === undefined
        ? await this.requireSpotNodeRuntime().dispatchEntryActorPacket(
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          )
        : await this.requireSpotManager().dispatchRoutedActorPacket(
            state.spotRid,
            relay.actorId,
            [header, body],
            true,
            remoteBoundSessionTarget
          );
      return {
        ok: true,
        response,
        actorPacketTarget: encodeRemoteActorPacketTarget(this.actorPacketTargetForState(relay.actorId))
      };
    } catch (error) {
      return {
        ok: false,
        error: error instanceof Error ? error.message : String(error)
      };
    } finally {
      header.close();
      body.close();
    }
  }

  private async relayRemoteActorPacket(
    actor: ZLinkSessionActor,
    frameHeader: ZLinkStreamFrameHeader,
    payload: Message,
    signal?: AbortSignal
  ): Promise<boolean> {
    const remoteTarget = this.actorManager?.getState(actor.actorId)?.remoteActorPacketTarget
      ?? this.sessionActorPacketTargets.get(actor)
      ?? this.sessionActorPacketTargetsByActor.get(sessionActorPacketTargetKey(actor))
      ?? this.remoteActorPacketTargetForBoundActor(actor.ref);
    if (remoteTarget === undefined) {
      return false;
    }
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    const request = {
      packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
      actorId: actor.actorId,
      routerChannelId: remoteTarget.routerChannelId,
      boundSessionTargetNodeRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      boundSessionSpotRid: localNodeRid === undefined ? undefined : String(localNodeRid),
      header: Buffer.from(encodeStreamHeader(frameHeader)).toString('base64'),
      payload: Buffer.from(messageToBytes(payload)).toString('base64')
    };
    try {
      let reply: {
        readonly ok?: boolean;
        readonly error?: unknown;
        readonly response?: unknown;
        readonly actorPacketTarget?: unknown;
      };
      const payload = BindingMessage.from(Buffer.from(JSON.stringify(request)));
      try {
        const parts = await this.routeTransport.requestRawToSpot({
          ...remoteTarget,
          spotKind: remoteTarget.spotKind ?? ZLinkSpotKind.User
        }, payload, {
          timeoutMs: this.options.registration.requestTimeoutMs,
          signal
        });
        try {
          if (parts.length === 0) {
            throw new Error(`Remote actor packet relay reply was empty for '${actor.actorId}'.`);
          }
          reply = JSON.parse(parts[0].getString('utf8')) as {
            readonly ok?: boolean;
            readonly error?: unknown;
            readonly response?: unknown;
            readonly actorPacketTarget?: unknown;
          };
        } finally {
          parts.forEach((part) => part.close());
        }
      } finally {
        payload.close();
      }
      if (frameHeader.requestSeq !== undefined) {
          if (reply.ok === false) {
            const sent = this.streamBindingRuntime.sendLocalBoundSessionError(
              actor.actorId,
              frameHeader.name,
              frameHeader.requestSeq,
              reply.error ?? 'Remote actor request failed.',
              streamMetadataMap(frameHeader.metadata)
            );
            if (!sent) {
              throw new Error(`Actor '${actor.actorId}' local bound session error response route is not ready.`);
            }
            return true;
          }
          const actorPacketTarget = decodeRemoteActorPacketTarget(reply.actorPacketTarget);
          if (
            actorPacketTarget !== undefined &&
            (localNodeRid === undefined || !routingIdsEqual(actorPacketTarget.targetNodeRid, localNodeRid))
          ) {
            this.actorManager?.getState(actor.actorId)?.setRemoteActorPacketTarget(actorPacketTarget);
            this.sessionActorPacketTargets.set(actor, actorPacketTarget);
            this.sessionActorPacketTargetsByActor.set(sessionActorPacketTargetKey(actor), actorPacketTarget);
          }
          const sent = this.streamBindingRuntime.sendLocalBoundSessionResponse(
            actor.actorId,
            frameHeader.name,
            frameHeader.requestSeq,
            reply.response,
            streamMetadataMap(frameHeader.metadata)
          );
          if (!sent) {
            throw new Error(`Actor '${actor.actorId}' local bound session response route is not ready.`);
          }
      }
    } finally {
      void request;
    }
    return true;
  }

  private actorPacketTargetForState(actorId: string): ZLinkRemoteActorPacketTarget | undefined {
    const state = this.actorManager?.getState(actorId);
    if (
      state?.remoteActorPacketTarget !== undefined &&
      (state.spotRid === undefined || String(state.remoteActorPacketTarget.spotRid) === String(state.spotRid))
    ) {
      return state.remoteActorPacketTarget;
    }
    const spotRid = state?.spotRid;
    if (spotRid !== undefined && state?.remoteActorPacketTarget !== undefined) {
      return {
        routerChannelId: state.remoteActorPacketTarget.routerChannelId,
        targetNodeRid: state.remoteActorPacketTarget.targetNodeRid,
        spotRid: normalizeRuntimeRoutingId(spotRid),
        spotKind: ZLinkSpotKind.User
      };
    }
    const actorRef = state?.nativeActorRef as ActorRef | undefined;
    const targetNodeRid = actorRef?.nodeRid as RoutingId | undefined
      ?? this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    const routerChannelId = this.options.registration.registrySpotRemoteAddresses?.routerChannelId
      ?? this.firstAcceptedSpotRouteChannel();
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    if (targetNodeRid !== undefined && localNodeRid !== undefined && routingIdsEqual(targetNodeRid, localNodeRid)) {
      return undefined;
    }
    if (spotRid === undefined || targetNodeRid === undefined || routerChannelId === undefined) {
      return undefined;
    }
    return {
      routerChannelId,
      targetNodeRid: normalizeRuntimeRoutingId(targetNodeRid),
      spotRid: normalizeRuntimeRoutingId(spotRid),
      spotKind: ZLinkSpotKind.User
    };
  }

  private remoteActorPacketTargetForBoundActor(actorRef: ActorRef): ZLinkRemoteActorPacketTarget | undefined {
    const targetNodeRid = actorRef.nodeRid as RoutingId;
    const localNodeRid = this.spotNodeRuntime?.primaryNode?.routingId as RoutingId | undefined;
    if (localNodeRid !== undefined && routingIdsEqual(localNodeRid, targetNodeRid)) {
      return undefined;
    }
    const configured = this.options.registration.registrySpotRemoteAddresses;
    const routerChannelId = configured?.routerChannelId ?? this.firstAcceptedSpotRouteChannel();
    if (routerChannelId === undefined) {
      return undefined;
    }
    return {
      routerChannelId,
      targetNodeRid,
      spotRid: targetNodeRid,
      spotKind: ZLinkSpotKind.Entry
    };
  }

  private firstAcceptedSpotRouteChannel(): string | undefined {
    for (const spotNode of this.options.registration.spotNodes.values()) {
      const channelName = Object.keys(spotNode.acceptedSpotRouteChannels ?? {})[0];
      if (Object.keys(spotNode.acceptedSpotRouteChannels ?? {}).length > 0) {
        return channelName;
      }
    }
    return undefined;
  }
}

function decodeRemoteActorPacketTarget(value: unknown): ZLinkRemoteActorPacketTarget | undefined {
  if (
    typeof value !== 'object' ||
    value === null ||
    typeof (value as { routerChannelId?: unknown }).routerChannelId !== 'string' ||
    typeof (value as { targetNodeRid?: unknown }).targetNodeRid !== 'string' ||
    typeof (value as { spotRid?: unknown }).spotRid !== 'string'
  ) {
    return undefined;
  }
  return {
    routerChannelId: (value as { routerChannelId: string }).routerChannelId,
    targetNodeRid: decodeWireRoutingId(
      (value as { targetNodeRid: string }).targetNodeRid,
      (value as { targetNodeRidHex?: unknown }).targetNodeRidHex
    ),
    spotRid: decodeWireRoutingId(
      (value as { spotRid: string }).spotRid,
      (value as { spotRidHex?: unknown }).spotRidHex
    ),
    spotKind: (value as { spotKind?: unknown }).spotKind === ZLinkSpotKind.Entry
      ? ZLinkSpotKind.Entry
      : ZLinkSpotKind.User
  };
}

function normalizeRuntimeRoutingId(value: RoutingId | string): RoutingId {
  const raw = value as unknown;
  return raw instanceof BindingRoutingId
    ? raw as unknown as RoutingId
    : BindingRoutingId.from(String(value)) as unknown as RoutingId;
}

function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  return typeof toHex === 'function' ? toHex.call(routingId) : undefined;
}

function decodeWireRoutingId(text: string, hex: unknown): RoutingId {
  return typeof hex === 'string'
    ? BindingRoutingId.fromHex(hex) as unknown as RoutingId
    : normalizeRuntimeRoutingId(text);
}

function encodeRemoteActorPacketTarget(target: ZLinkRemoteActorPacketTarget | undefined): {
  readonly routerChannelId: string;
  readonly targetNodeRid: string;
  readonly targetNodeRidHex?: string;
  readonly spotRid: string;
  readonly spotRidHex?: string;
  readonly spotKind: ZLinkSpotKind;
} | undefined {
  if (target === undefined) {
    return undefined;
  }
  return {
    routerChannelId: target.routerChannelId,
    targetNodeRid: String(target.targetNodeRid),
    targetNodeRidHex: encodeRoutingIdHex(target.targetNodeRid),
    spotRid: String(target.spotRid),
    spotRidHex: encodeRoutingIdHex(target.spotRid),
    spotKind: target.spotKind ?? ZLinkSpotKind.User
  };
}

function sessionActorPacketTargetKey(actor: ZLinkSessionActor): string {
  return `${String(actor.ref.nodeRid)}:${actor.actorId}:${String(actor.ref.generation)}`;
}

function decodeRemoteActorPacketRelayPayload(payload: unknown): {
  readonly actorId: string;
  readonly routerChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly header: string;
  readonly payload: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { header?: unknown }).header !== 'string' ||
    typeof (payload as { payload?: unknown }).payload !== 'string'
  ) {
    throw new Error('Remote actor packet relay payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    header: (payload as { header: string }).header,
    payload: (payload as { payload: string }).payload
  };
}

function decodeRemoteActorJoinPayload(payload: unknown): {
  readonly spotRid: string;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly sourceSpotRid?: string;
  readonly routerChannelId?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionSpotRid?: string;
  readonly request: string;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_ACTOR_JOIN_PACKET ||
    typeof (payload as { spotRid?: unknown }).spotRid !== 'string' ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { actorType?: unknown }).actorType !== 'string' ||
    typeof (payload as { actorNodeRid?: unknown }).actorNodeRid !== 'string' ||
    typeof (payload as { actorGeneration?: unknown }).actorGeneration !== 'string' ||
    typeof (payload as { request?: unknown }).request !== 'string'
  ) {
    throw new Error('Remote actor join payload is invalid.');
  }
  return {
    spotRid: (payload as { spotRid: string }).spotRid,
    actorId: (payload as { actorId: string }).actorId,
    actorType: (payload as { actorType: string }).actorType,
    actorNodeRid: (payload as { actorNodeRid: string }).actorNodeRid,
    actorNodeRidHex: typeof (payload as { actorNodeRidHex?: unknown }).actorNodeRidHex === 'string'
      ? (payload as { actorNodeRidHex: string }).actorNodeRidHex
      : undefined,
    actorGeneration: (payload as { actorGeneration: string }).actorGeneration,
    sourceSpotRid: typeof (payload as { sourceSpotRid?: unknown }).sourceSpotRid === 'string'
      ? (payload as { sourceSpotRid: string }).sourceSpotRid
      : undefined,
    routerChannelId: typeof (payload as { routerChannelId?: unknown }).routerChannelId === 'string'
      ? (payload as { routerChannelId: string }).routerChannelId
      : undefined,
    boundSessionRouterChannelId: typeof (payload as { boundSessionRouterChannelId?: unknown }).boundSessionRouterChannelId === 'string'
      ? (payload as { boundSessionRouterChannelId: string }).boundSessionRouterChannelId
      : undefined,
    boundSessionTargetNodeRid: typeof (payload as { boundSessionTargetNodeRid?: unknown }).boundSessionTargetNodeRid === 'string'
      ? (payload as { boundSessionTargetNodeRid: string }).boundSessionTargetNodeRid
      : undefined,
    boundSessionSpotRid: typeof (payload as { boundSessionSpotRid?: unknown }).boundSessionSpotRid === 'string'
      ? (payload as { boundSessionSpotRid: string }).boundSessionSpotRid
      : undefined,
    request: (payload as { request: string }).request
  };
}

function decodeRemoteBoundSessionSendPayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName?: string;
  readonly metadata?: Record<string, string>;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string'
  ) {
    throw new Error('Remote bound session send payload is invalid.');
  }
  const rawMetadata = (payload as { metadata?: unknown }).metadata;
  const metadata: Record<string, string> = {};
  if (typeof rawMetadata === 'object' && rawMetadata !== null) {
    for (const [key, value] of Object.entries(rawMetadata)) {
      if (typeof value === 'string') {
        metadata[key] = value;
      }
    }
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: typeof (payload as { boundPacketName?: unknown }).boundPacketName === 'string'
      ? (payload as { boundPacketName: string }).boundPacketName
      : undefined,
    metadata
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
    const remoteTarget = this.remoteBoundSessionTargetProvider();
    if (remoteTarget !== undefined) {
      const payload = {
        packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET,
        actorId: this.actorId,
        message: this.message,
        boundPacketName: this.selectedPacketName,
        metadata: Object.fromEntries(this.selectedMetadata)
      };
      await this.routedTransport.sendToSpot(
        {
          routerChannelId: remoteTarget.routerChannelId,
          targetNodeRid: remoteTarget.targetNodeRid,
          spotRid: remoteTarget.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        payload,
        { packetName: ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET, signal }
      );
      return;
    }
    if (this.runtime.sendLocalBoundSession(
      this.actorId,
      this.message,
      this.selectedPacketName,
      this.selectedMetadata
    )) {
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
    const nativeActorRef = state.nativeActorRef;
    return {
      resultCode: result.accepted ? 0 : 1,
      actor: nativeActorRef === undefined
        ? localActorRef(nodeRidForLocalActor(this.options.nativeNode), actor.actorId)
        : {
            nodeRid: nativeActorRef.nodeRid as unknown as RoutingId,
            actorId: nativeActorRef.actorId,
            generation: nativeActorRef.generation
          } as ActorRef,
      reply: result.reply as Message | undefined
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

function routingIdsEqual(left: RoutingId, right: RoutingId): boolean {
  const leftHex = (left as { toHex?: () => string }).toHex?.();
  const rightHex = (right as { toHex?: () => string }).toHex?.();
  if (leftHex !== undefined && rightHex !== undefined) {
    return leftHex === rightHex;
  }
  return String(left) === String(right);
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
