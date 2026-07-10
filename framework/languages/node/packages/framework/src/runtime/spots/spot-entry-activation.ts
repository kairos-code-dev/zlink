import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkEntrySpot,
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotContext,
  ZLinkEntrySpotPacketHandlerRegistration,
  ZLinkEntrySpotSubscriptionHandlerRegistration,
  ZLinkEntrySpotTimerHandlerRegistration,
  ZLinkFanoutClient,
  ZLinkMessage,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotPublisherClient
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkRemoteActorPacketTarget,
  ZLinkRemoteBoundSessionTarget
} from '../actors';
import {
  ZLinkActorDispatchMailboxSet,
  ZLinkSpotActorHandlerRegistryRuntime
} from '../actors';
import type {
  ZLinkBackendActorRecvInfo,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkSpotWorkerRuntime } from '../workers';
import {
  ZLinkSpotActorPacketDispatch,
  type ZLinkActorResponseOptions
} from './spot-actor-packet-dispatch';
import { ZLinkSpotActorJoinDispatch } from './spot-actor-join-dispatch';
import {
  applyEntrySpotHandlerRegistrations,
  DefaultZLinkSpotHandlerRegistry
} from './spot-handler-registry';
import {
  DefaultZLinkSpotOutbound,
  type ZLinkSpotRoutedTransport
} from './spot-outbound';
import { createProviderInstance } from './spot-provider';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import {
  addEntrySpotTimerRegistrations,
  ZLinkSpotTimerRegistry
} from './spot-timer';
import { createEntrySpotContext } from './spot-context';
import type { RequestResult } from '@zlink-systems/zlink';

interface ZLinkEntrySpotActivationOptions {
  readonly entrySpotType: Type<ZLinkEntrySpot>;
  readonly timerHandlers?: readonly ZLinkEntrySpotTimerHandlerRegistration[];
  readonly packetHandlers?: readonly ZLinkEntrySpotPacketHandlerRegistration[];
  readonly subscriptionHandlers?: readonly ZLinkEntrySpotSubscriptionHandlerRegistration[];
  readonly actorSendHandlers?: readonly ZLinkEntrySpotActorSendHandlerRegistration[];
  readonly actorRequestHandlers?: readonly ZLinkEntrySpotActorRequestHandlerRegistration[];
  readonly nativeSpot: ZLinkBackendSpot;
  readonly nativeNode: ZLinkBackendSpotNode;
  readonly nodeRid: RoutingId;
  readonly spotNodeName: string;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime?: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly entryActorCommitter?: (actor: ZLinkActor) => Promise<void> | void;
  readonly destroyActor?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionReceiver?: (
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionResponseReceiver?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    message: unknown,
    replyOptions: ZLinkActorResponseOptions,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly routedBoundSessionErrorReceiver?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorPacketTarget?: unknown,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorResponseSender?: (
    actor: ZLinkActor,
    packetName: string,
    requestSeq: bigint,
    response: unknown,
    replyOptions: ZLinkActorResponseOptions,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorErrorSender?: (
    actorId: string,
    packetName: string,
    requestSeq: bigint,
    error: unknown,
    metadata: ReadonlyMap<string, string>,
    actorRef?: ActorRef,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly remoteActorPacketTargetReceiver?: (
    actorId: string,
    target: ZLinkRemoteBoundSessionTarget
  ) => void;
  readonly remoteBoundSessionTargetResolver?: (
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly actorPacketTargetProvider?: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
  readonly localActorPacketRouter?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ) => Promise<{ readonly handled: boolean; readonly response?: unknown }>;
}

export class ZLinkEntrySpotActivation {
  private readonly serial = new ZLinkSpotSerialExecutor();
  private readonly actorPacketMailboxes = new ZLinkActorDispatchMailboxSet();
  private readonly timers = new ZLinkSpotTimerRegistry();
  private readonly actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
  private readonly handlers = new DefaultZLinkSpotHandlerRegistry(this.actorHandlers);
  private readonly outbound: DefaultZLinkSpotOutbound;
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;
  private initialized = false;

  entrySpot: ZLinkEntrySpot;
  readonly context: ZLinkEntrySpotContext;

  constructor(private readonly options: ZLinkEntrySpotActivationOptions) {
    // Entry Spot lifecycle, timers and detached continuations use this serial
    // executor. Actor packets use the target actor mailbox.
    this.outbound = new DefaultZLinkSpotOutbound(
      this.serial,
      options.channelClient,
      options.fanoutClient,
      options.spotPublisherClient,
      options.routedTransport,
      options.spotRouterChannelIdForMesh ?? ((meshName) => meshName)
    );
    this.workerRuntime = options.workerRuntime ?? new ZLinkSpotWorkerRuntime();
    this.context = createEntrySpotContext({
      nativeSpotRid: options.nativeSpot.routingId,
      nodeRid: options.nodeRid,
      handlers: this.handlers,
      outbound: this.outbound,
      timers: this.timers,
      serial: this.serial,
      getEntrySpot: () => this.entrySpot,
      spotNodeName: options.spotNodeName,
      providerResolver: options.providerResolver,
      runtimeEventPublisher: options.runtimeEventPublisher,
      workerRuntime: this.workerRuntime,
      destroyActor: options.destroyActor
    });
    this.entrySpot = undefined as unknown as ZLinkEntrySpot;
    applyEntrySpotHandlerRegistrations(this.handlers, options.entrySpotType, {
      actorSendHandlers: options.actorSendHandlers,
      actorRequestHandlers: options.actorRequestHandlers,
      packetHandlers: options.packetHandlers,
      subscriptionHandlers: options.subscriptionHandlers
    });
  }

  /**
   * The Entry Spot serial dispatch line for Entry Spot-owned callbacks such as
   * lifecycle, timers, request continuations and worker completions. Actor
   * packets use the target actor mailbox instead.
   */
  get serialExecutor(): ZLinkSpotSerialExecutor {
    return this.serial;
  }

  get nodeRid(): RoutingId {
    return this.options.nodeRid;
  }

  async create(): Promise<void> {
    const entrySpot = await createProviderInstance(this.options.entrySpotType, this.options.providerResolver, this.context);
    this.entrySpot = entrySpot;
    Object.defineProperty(this.entrySpot, 'context', {
      configurable: true,
      enumerable: false,
      value: this.context
    });
    await addEntrySpotTimerRegistrations(
      this.timers,
      this.options.entrySpotType,
      this.entrySpot,
      this.serial,
      { timerHandlers: this.options.timerHandlers },
      {
        providerResolver: this.options.providerResolver,
        spotNodeName: this.options.spotNodeName,
        nodeRid: this.options.nodeRid,
        runtimeEventPublisher: this.options.runtimeEventPublisher
      }
    );
  }

  async configure(): Promise<void> {
    await this.entrySpot.configure?.();
  }

  async initialize(): Promise<void> {
    await this.serial.execute(() => this.entrySpot.onInitialize?.());
    this.attachActorJoinDispatch();
    this.initialized = true;
  }

  async dispose(): Promise<void> {
    if (this.initialized) {
      await this.serial.execute(() => this.entrySpot.onClosing?.());
    }
    await this.timers.dispose();
    if (typeof this.options.nativeSpot.dispose === 'function') {
      await this.options.nativeSpot.dispose();
    }
  }

  notifyCreateActor(actor: ZLinkActor, createRequest: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onCreateActor?.(actor, createRequest, signal));
  }

  notifyJoinActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onJoinedActor?.(actor, signal));
  }

  /**
   * Entry Spot join admission uses the shared core round-trip. Re-entry defaults
   * to accept when the Entry Spot does not declare `onActorJoin` (actor returning
   * home), so `defaultAccept` is true.
   */
  private attachActorJoinDispatch(): void {
    const dispatch = new ZLinkSpotActorJoinDispatch({
      nativeSpot: this.options.nativeSpot,
      serial: this.serial,
      resolveActor: (actorId) => this.options.actorResolver?.(actorId),
      getTarget: () => this.entrySpot,
      defaultAccept: true,
      commitRoutedActor: this.options.entryActorCommitter,
      actorPacketHandler: (actorId, parts, returnResponse, remoteBoundSessionTarget) =>
        this.dispatchActorPacket(actorId, parts, returnResponse, remoteBoundSessionTarget),
      routedBoundSessionReceiver: this.options.routedBoundSessionReceiver,
      routedBoundSessionResponseReceiver: this.options.routedBoundSessionResponseReceiver,
      routedBoundSessionErrorReceiver: this.options.routedBoundSessionErrorReceiver,
      actorPacketTargetProvider: this.options.actorPacketTargetProvider,
      bindRemoteActorSession: (actor, sourceNodeRid, sourceSessionRid) => {
        if (String(sourceNodeRid) === String(this.options.nativeNode.routingId)) {
          return;
        }
        const target = this.options.remoteBoundSessionTargetResolver?.(sourceNodeRid, sourceSessionRid);
        if (target !== undefined) {
          this.options.remoteActorPacketTargetReceiver?.(actor.actorId, target);
        }
        this.options.nativeNode.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid);
      },
      replyActorNoBind: (info, parts, result) => this.replyActorNoBind(info, parts, result),
      messageSerializers: this.options.messageSerializers,
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors
    });
    dispatch.configureSubscriptions(this.handlers.snapshot());
    dispatch.attach();
  }

  notifyLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onLeaveActor?.(actor, signal));
  }

  notifyDisconnectActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    return this.serial.execute(() => this.entrySpot.onDisconnectActor?.(actor, signal));
  }

  async dispatchActorPacket(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return this.actorPacketMailboxes.submit(actorId, () =>
      this.dispatchActorPacketInsideMailbox(
        actorId,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      ));
  }

  private async dispatchActorPacketInsideMailbox(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return new ZLinkSpotActorPacketDispatch({
      spot: this.entrySpot as unknown as ZLinkSpot,
      spotRid: () => String(this.options.nativeSpot.routingId),
      registry: this.actorHandlers,
      resolveActor: (targetActorId) => this.options.actorResolver?.(targetActorId),
      routeBeforeLocal: (targetActorId, targetParts, targetReturnResponse, targetRemoteBoundSessionTarget) =>
        this.options.localActorPacketRouter?.(
          targetActorId,
          targetParts,
          targetReturnResponse,
          targetRemoteBoundSessionTarget
        ),
      onRemoteBoundSessionTarget: (targetActorId, target) =>
        this.options.remoteActorPacketTargetReceiver?.(targetActorId, target),
      onDisconnectActor: (actor) => this.notifyDisconnectActor(actor),
      actorResponseSender: this.options.actorResponseSender,
      actorErrorSender: this.options.actorErrorSender,
      providerResolver: this.options.providerResolver,
      messageSerializers: this.options.messageSerializers,
      dispatchErrors: this.options.dispatchErrors
    }).dispatch(actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef);
  }

  private replyActorNoBind(
    info: ZLinkBackendActorRecvInfo,
    parts: readonly Message[],
    result: RequestResult
  ): void {
    this.options.nativeNode.replyActorNoBind(info, parts, result);
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
