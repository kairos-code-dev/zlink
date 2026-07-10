import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkMessageSerializer,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher,
  ZLinkSpot,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotCreateResult,
  ZLinkSpotCreateResponse,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotPublisherClient,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts';
import {
  isZLinkMessage,
  ZLinkSpotCreateState
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendActorJoinInfo,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { ZLinkDispatchErrorReporter } from '../channels';
import {
  ZLinkSpotActorHandlerRegistryRuntime,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import { ZLinkSpotWorkerRuntime } from '../workers';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import { createFreshProviderInstance } from './spot-provider';
import { DefaultZLinkSpotOutbound, type ZLinkSpotRoutedTransport } from './spot-outbound';
import {
  applySpotHandlerRegistrations,
  DefaultZLinkSpotHandlerRegistry
} from './spot-handler-registry';
import {
  addSpotTimerRegistrations,
  ZLinkSpotTimerRegistry
} from './spot-timer';
import { ZLinkSpotSerialExecutor } from './spot-serial-executor';
import { createSpotContext } from './spot-context';
import { ZLinkSpotActorJoinDispatch } from './spot-actor-join-dispatch';
import {
  ZLinkSpotActorPacketDispatch,
  type ZLinkActorResponseOptions
} from './spot-actor-packet-dispatch';
import type { ZLinkRemoteActorJoinActor, ZLinkRoutedActorTransferProvider } from './spot-remote-codec';
import type { ZLinkSpotActivation } from './spot-activation-registry';
import type { ZLinkSpotLocationClaim } from './spot-location-claim';

export interface ZLinkSpotActivationLifecycleOptions {
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
  readonly actorCountProvider?: (spotRid: RoutingId) => number;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationClaim: ZLinkSpotLocationClaim;
  readonly createNativeSpot?: (spotRid: RoutingId) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly routedActorProvider?: (
    actorId: string,
    actorType: string,
    actorRef?: ZLinkBackendActorRef,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    actorCreateRequest?: Message,
    signal?: AbortSignal
  ) => Promise<ZLinkRemoteActorJoinActor>;
  readonly routedActorTransferProvider?: ZLinkRoutedActorTransferProvider;
  readonly routedActorFinalizer?: (actor: ZLinkActor, spotRid: RoutingId) => Promise<void>;
  readonly routedActorCommitter?: (actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot) => void;
  readonly routedActorLeaveCommitter?: (actor: ZLinkActor) => void;
  readonly routedActorRollback?: (actor: ZLinkActor) => Promise<void>;
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
  readonly routedBoundSessionReceiver?: (
    actorId: string,
    message: unknown,
    packetName: string | undefined,
    metadata: ReadonlyMap<string, string>,
    actorRef?: ActorRef,
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
  readonly routedBoundSessionOwnershipReceiver?: (payload: unknown) => Promise<void>;
  readonly remoteActorPacketTargetReceiver?: (
    actorId: string,
    target: ZLinkRemoteBoundSessionTarget
  ) => void;
  readonly remoteBoundSessionTargetResolver?: (
    sourceNodeRid: RoutingId,
    sourceSessionRid: RoutingId
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly actorPacketTargetProvider?: (actorId: string) => ZLinkRemoteActorPacketTarget | undefined;
  readonly leaveActor: (spotRid: RoutingId, actor: ZLinkActor, signal?: AbortSignal) => Promise<void>;
  readonly closeSpot: (spotRid: RoutingId, signal?: AbortSignal) => Promise<boolean>;
  readonly registerActivation: (activation: ZLinkSpotActivation) => void;
}

interface UserSpotLocationClaim {
  readonly meshName: string;
}

export class ZLinkSpotActivationLifecycle {
  constructor(private readonly options: ZLinkSpotActivationLifecycleOptions) {}

  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const serial = new ZLinkSpotSerialExecutor();
    const actorHandlers = new ZLinkSpotActorHandlerRegistryRuntime();
    const handlers = new DefaultZLinkSpotHandlerRegistry(actorHandlers);
    applySpotHandlerRegistrations(handlers, spotType, {
      actorSendHandlers: this.options.spotActorSendHandlers,
      actorRequestHandlers: this.options.spotActorRequestHandlers,
      packetHandlers: this.options.spotPacketHandlers,
      subscriptionHandlers: this.options.spotSubscriptionHandlers
    });
    const timers = new ZLinkSpotTimerRegistry();
    let nativeSpot: ZLinkBackendSpot | undefined;
    const outbound = new DefaultZLinkSpotOutbound(
      serial,
      this.options.channelClient,
      this.options.fanoutClient,
      this.options.spotPublisherClient,
      this.options.routedTransport,
      this.options.spotRouterChannelIdForMesh ?? ((meshName) => meshName),
      () => nativeSpot
    );
    const locationClaim = await this.options.locationClaim.claimUserSpot(spotRid, spotType.name);
    if (!locationClaim.claimed) {
      return { spotRid, state: ZLinkSpotCreateState.Existing };
    }

    let spot: ZLinkSpot | undefined;
    const context = createSpotContext({
      spotRid,
      handlers,
      outbound,
      timers,
      serial,
      getSpot: () => spot,
      nodeRid: this.options.nodeRid,
      nodeRidProvider: this.options.nodeRidProvider,
      providerResolver: this.options.providerResolver,
      runtimeEventPublisher: this.options.runtimeEventPublisher,
      workerRuntime: this.options.workerRuntime,
      leaveActor: (actor, contextSignal) => this.options.leaveActor(spotRid, actor, contextSignal),
      close: (contextSignal) => this.options.closeSpot(spotRid, contextSignal)
    });
    try {
      spot = await createFreshProviderInstance(spotType, this.options.providerResolver, context);
    } catch (error) {
      await this.options.locationClaim.release(locationClaim.meshName, spotRid);
      throw error;
    }
    Object.defineProperty(spot, 'context', {
      configurable: true,
      enumerable: false,
      value: context
    });

    const actors = new Map<string, ZLinkActor>();
    const leftActors = new Set<string>();
    // getOrCreateSpot registers the native Spot under this rid so core routes
    // actor-join admission requests to it (createSpot alone does not register).
    nativeSpot = this.options.createNativeSpot?.(spotRid);
    const activation: ZLinkSpotActivation = {
      spotRid,
      spotType,
      spot,
      serial,
      timers,
      actors,
      leftActors,
      actorHandlers,
      handlers,
      actorCount: () => actors.size + (this.options.actorCountProvider?.(spotRid) ?? 0),
      nativeSpot
    };
    const nativeDispatch = this.attachNativeActorJoinDispatch(activation, nativeSpot);
    return await this.runCreateLifecycle(activation, spotType, request, locationClaim, nativeDispatch, signal);
  }

  async close(activation: ZLinkSpotActivation, signal?: AbortSignal): Promise<void> {
    await activation.serial.execute(() => this.closeInsideSerial(activation, signal));
  }

  async closeInsideSerial(activation: ZLinkSpotActivation, signal?: AbortSignal): Promise<void> {
    try {
      await activation.spot.onClosing?.(signal);
    } finally {
      await activation.timers.dispose();
      if (activation.nativeSpot !== undefined && typeof activation.nativeSpot.dispose === 'function') {
        await activation.nativeSpot.dispose();
      }
      await this.options.locationClaim.release(
        this.options.locationClaim.meshNameForRelease(),
        activation.spotRid
      );
    }
  }

  async dispatchActorPacket(
    activation: ZLinkSpotActivation,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return new ZLinkSpotActorPacketDispatch({
      spot: activation.spot,
      spotRid: () => String(activation.spotRid),
      registry: activation.actorHandlers,
      serial: activation.serial,
      resolveActor: (targetActorId) =>
        activation.actors.get(targetActorId) ?? this.options.actorResolver?.(targetActorId),
      actorLeft: (targetActorId) => activation.leftActors.has(targetActorId),
      onRemoteBoundSessionTarget: (targetActorId, target) =>
        this.options.remoteActorPacketTargetReceiver?.(targetActorId, target),
      onDisconnectActor: (actor) =>
        activation.serial.execute(() => activation.spot.onDisconnectActor?.(actor)),
      actorResponseSender: this.options.actorResponseSender,
      actorErrorSender: this.options.actorErrorSender,
      providerResolver: this.options.providerResolver,
      messageSerializers: this.options.messageSerializers,
      dispatchErrors: this.options.dispatchErrors
    }).dispatch(actorId, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef);
  }

  private attachNativeActorJoinDispatch(
    activation: ZLinkSpotActivation,
    nativeSpot: ZLinkBackendSpot | undefined
  ): ZLinkSpotActorJoinDispatch | undefined {
    if (nativeSpot === undefined) {
      return undefined;
    }
    const nativeDispatch = new ZLinkSpotActorJoinDispatch({
      nativeSpot,
      serial: activation.serial,
      resolveActor: (actorId) => activation.leftActors.has(actorId)
        ? undefined
        : activation.actors.get(actorId) ?? this.options.actorResolver?.(actorId),
      getTarget: () => activation.spot,
      defaultAccept: false,
      routedActorProvider: this.options.routedActorProvider,
      routedActorTransferProvider: this.options.routedActorTransferProvider,
      finalizeRoutedActor: (actor) => this.options.routedActorFinalizer?.(actor, activation.spotRid),
      rollbackRoutedActor: async (actor) => {
        activation.leftActors.add(actor.actorId);
        activation.actors.delete(actor.actorId);
        this.options.routedActorLeaveCommitter?.(actor);
        await this.options.routedActorRollback?.(actor);
      },
      rollbackNativeActor: async (actor) => {
        activation.leftActors.add(actor.actorId);
        activation.actors.delete(actor.actorId);
        this.options.routedActorLeaveCommitter?.(actor);
      },
      commitRoutedActor: (actor) => {
        this.options.routedActorCommitter?.(actor, activation.spotRid, activation.spot);
        activation.leftActors.delete(actor.actorId);
        activation.actors.set(actor.actorId, actor);
      },
      actorPacketHandler: (actorId, parts, returnResponse, remoteBoundSessionTarget) =>
        this.dispatchActorPacket(activation, actorId, parts, returnResponse, remoteBoundSessionTarget),
      routedBoundSessionReceiver: this.options.routedBoundSessionReceiver,
      routedBoundSessionResponseReceiver: this.options.routedBoundSessionResponseReceiver,
      routedBoundSessionErrorReceiver: this.options.routedBoundSessionErrorReceiver,
      routedBoundSessionOwnershipReceiver: this.options.routedBoundSessionOwnershipReceiver,
      actorPacketTargetProvider: this.options.actorPacketTargetProvider,
      bindRemoteActorSession: (actor, sourceNodeRid, sourceSessionRid) => {
        const node = this.options.nativeSpotNodeProvider?.();
        if (node === undefined || String(sourceNodeRid) === String(node.routingId)) {
          return;
        }
        const target = this.options.remoteBoundSessionTargetResolver?.(sourceNodeRid, sourceSessionRid);
        if (target !== undefined) {
          this.options.remoteActorPacketTargetReceiver?.(actor.actorId, target);
        }
        node.bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid);
      },
      replyActorNoBind: (info, parts, result) =>
        this.options.nativeSpotNodeProvider?.()?.replyActorNoBind(info, parts, result),
      messageSerializers: this.options.messageSerializers,
      providerResolver: this.options.providerResolver,
      dispatchErrors: this.options.dispatchErrors
    });
    nativeDispatch.attach();
    return nativeDispatch;
  }

  private async runCreateLifecycle<TSpot extends ZLinkSpot>(
    activation: ZLinkSpotActivation,
    spotType: Type<TSpot>,
    request: Message,
    locationClaim: UserSpotLocationClaim,
    nativeDispatch: ZLinkSpotActorJoinDispatch | undefined,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    try {
      await activation.spot.configure?.();
      nativeDispatch?.configureSubscriptions(activation.handlers.snapshot());
      await addSpotTimerRegistrations(
        activation.timers,
        spotType,
        activation.spotRid,
        activation.spot,
        activation.serial,
        { timerHandlers: this.options.spotTimerHandlers },
        {
          providerResolver: this.options.providerResolver,
          runtimeEventPublisher: this.options.runtimeEventPublisher,
          signal
        }
      );
      let createResponse: ZLinkSpotCreateResponse | undefined;
      await activation.serial.execute(async () => {
        createResponse = await activation.spot.onCreate?.(
          wrapFrameworkPayloadMessage(request, this.options.messageSerializers),
          signal
        );
        if (createResponse?.accepted === false) {
          return;
        }
        await activation.spot.onInitialize?.(signal);
      });
      if (createResponse?.accepted === false) {
        await activation.timers.dispose();
        await this.options.locationClaim.release(locationClaim.meshName, activation.spotRid);
        return {
          spotRid: activation.spotRid,
          state: ZLinkSpotCreateState.Rejected,
          reply: this.decodeCreateReply(createResponse.reply)
        };
      }
      this.options.registerActivation(activation);
      return {
        spotRid: activation.spotRid,
        state: ZLinkSpotCreateState.Created,
        reply: this.decodeCreateReply(createResponse?.reply)
      };
    } catch (error) {
      await this.close(activation, signal);
      await this.options.locationClaim.release(locationClaim.meshName, activation.spotRid);
      throw error;
    }
  }

  private decodeCreateReply(reply: unknown): unknown {
    if (reply === undefined) {
      return undefined;
    }
    const message = encodeFrameworkPayloadMessage(reply, this.options.messageSerializers);
    try {
      return decodeFrameworkPayloadMessage(message, this.options.messageSerializers);
    } finally {
      if (ownsFrameworkPayloadMessage(reply)) {
        message.close();
      }
    }
  }
}

function ownsFrameworkPayloadMessage(value: unknown): boolean {
  return value === undefined || !(isMessage(value) || (isZLinkMessage(value) && value.isEncoded()));
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}
