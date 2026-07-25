import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkMessageSerializer,
  ZLinkInstanceSpot,
  ZLinkActor,
  ZLinkActorJoinRequest,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotInfo,
  ZLinkSpotPublisherClient,
} from '../../contracts';
import type {
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts/Configuration/RegistrationTypes';
export { createSpotHandle, resolveSpotHandle } from './spot-handle';
import type { ZLinkSpotRouteResolver, ZLinkSpotRouteTarget } from './spot-routing-internal';
import type { Message } from '../../contracts/Common/Message';
import { awaitWithAbort, throwIfAborted } from '../abort';
import {
  ZLinkMessage,
  ZLinkSpotCloseReason,
  ZLinkSpotKind,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import {
  Message as BindingMessage,
  SubmitResult
} from '@zlink-systems/zlink';
import {
  ActorLifecycleKind,
  OperationKind,
  ReceiveKind,
  type ReadyRecord,
  type ReceiveRecord
} from '../foundation/service-runtime-contracts';
import {
  ZLinkConfigurationException
} from '../configuration';
import type {
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';

import { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkWorkerRuntime } from '../workers';
import {
  createActorJoinRequest,
  createActorMembership,
  type ZLinkDeferredJoinAcceptedRoot,
  type ZLinkRemoteBoundSessionTarget
} from '../actors';
import type { ZLinkLocationLifecycle } from '../locations';
import {
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import {
  decodeChannelEnvelope,
  decodeChannelPayload,
  encodeChannelErrorReplyParts,
  encodeChannelReplyParts
} from '../channels/channel-envelope';
import {
  decodeStreamHeader,
  encodeStreamHeader,
  ZLinkStreamCodec,
  ZLinkStreamHeaderFlags,
  ZLinkStreamMessageKind
} from '../streams/protocol';
import {
  REMOTE_ACTOR_JOIN_PACKET,
  type ZLinkRemoteActorJoinWirePayload
} from '../actors/actor-remote-wire';
import { replayActorHandoffBacklog, type ZLinkActorHandoffPacket } from '../actors/actor-handoff';
import {
  decodeHandoffBacklog,
  decodeRemoteActorRef,
  decodeRemoteBoundSessionTarget
} from './spot-remote-codec';
import { decodeRoutingId } from '../routing-id';
export { ZLinkSpotSerialExecutor } from './spot-serial-executor';
export {
  ZLinkManagedTimer,
  ZLinkSpotTimerRegistry
} from './spot-timer';
export {
  DefaultZLinkSpotOutbound,
  type ZLinkSpotAddressCallOptions,
  type ZLinkSpotAddressTransport,
  type ZLinkSpotRoutedRequestOptions,
  type ZLinkSpotRoutedSendOptions,
  type ZLinkSpotRoutedTransport
} from './spot-outbound';
import {
  type ZLinkSpotRoutedTransport
} from './spot-outbound';
export {
  DefaultZLinkSpotHandlerRegistry,
  type ZLinkSpotHandlerRegistration
} from './spot-handler-registry';
export { ZLinkRuntimeSpotPublisherTransport } from './spot-publisher-transport';
import {
  ZLinkSpotActivationRegistry
} from './spot-activation-registry';
import type { ZLinkSpotActivation } from './spot-activation-state';
import {
  ZLinkSpotActivationLifecycle,
  type ZLinkNativeSpotAuthority
} from './spot-activation';
import { ZLinkSpotActorMembership, type ZLinkActorJoinRollback } from './spot-actor-membership';
import type {
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';
import type { ZLinkRuntimeAdmissionGate } from '../admission';
import type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import type { ZLinkLocalSpotCreateResult } from './spot-manager-internal-contracts';
export type { ZLinkLocalSpotCreateResult } from './spot-manager-internal-contracts';
export type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import { ZLinkSpotLocationClaim } from './spot-location-claim';
import { ZLinkRoutedSpotPacketDispatch } from './spot-routed-spot-packet-dispatch';
export { ZLinkEntrySpotActivation } from './spot-entry-activation';
export {
  createFrameworkEntrySpotId,
  ZLinkSpotNodeRuntimeManager,
  type ZLinkSpotNodeRuntimeManagerOptions
} from './spot-node-runtime-manager';
export {
  ZLinkPublicSpotManager,
  type ZLinkPublicSpotManagerOptions
} from './spot-manager-public';

export interface ZLinkSpotManagerOptions {
  readonly spotFactories: readonly Type<ZLinkSpot>[];
  readonly instanceSpotFactories?: ReadonlyMap<
    string,
    ReadonlyMap<string, Type<ZLinkInstanceSpot>>
  >;
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: (meshName: string) => RoutingId | undefined;
  readonly nodeGenerationProvider?: (meshName: string) => bigint | undefined;
  readonly entryNodeRid?: RoutingId;
  readonly entryNodeRidProvider?: () => RoutingId | undefined;
  readonly entrySpotCallbacks?: {
    onLeaveActor(
      actor: ZLinkActor,
      signal?: AbortSignal,
      actorRef?: ActorRef,
      membershipEpoch?: bigint
    ): Promise<void>;
  };
  readonly dispatchEntryActorPacket?: (
    actorId: string,
    parts: readonly Message[],
    returnResponse?: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<unknown>;
  readonly actorCountProvider?: (spotId: RoutingId) => number;
  readonly userSpotExecutionMode?: (
    meshName: string,
    spotType: Type<ZLinkSpot>
  ) => import('../../contracts').ZLinkUserSpotExecutionMode;
  readonly actorDispatchOwnerResolver?: (actorId: string) => {
    readonly actorRef?: ActorRef;
    readonly spotId?: RoutingId;
  };
  readonly actorBindingGenerationObserver?: (actorId: string, generation: bigint) => void;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly addressTransport?: import('./spot-outbound').ZLinkSpotAddressTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly channelMeshNameForChannel?: (channelName: string) => string | undefined;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime?: ZLinkWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  // Backs each user Spot with a core-native Spot object (registered for join
  // routing by rid) so actor-join admission uses the same recv/reply round-trip
  // as the Entry Spot and .NET, for local and remote callers alike.
  readonly createNativeSpot?: (
    meshName: string,
    spotId: RoutingId,
    authority?: ZLinkNativeSpotAuthority
  ) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: (meshName: string) => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly actorLifecycleResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly admission?: ZLinkRuntimeAdmissionGate;
}

export class DefaultZLinkSpotManager {
  private readonly factories: ReadonlySet<Type<ZLinkSpot>>;
  private readonly activations: ZLinkSpotActivationRegistry;
  private readonly workerRuntime: ZLinkWorkerRuntime;
  private readonly locationClaim: ZLinkSpotLocationClaim;
  private readonly routedSpotPackets: ZLinkRoutedSpotPacketDispatch;
  private readonly actorMembership: ZLinkSpotActorMembership;
  private readonly activationLifecycle: ZLinkSpotActivationLifecycle;
  private readonly formalRemoteTransfers = new Map<string, {
    readonly actor: ZLinkActor;
    readonly spotId: RoutingId;
    readonly transferId: string;
    readonly handoffBacklog: readonly ZLinkActorHandoffPacket[];
    readonly deferredJoinRoot?: ZLinkDeferredJoinAcceptedRoot;
    readonly sourceLeaveTerminal: Promise<boolean>;
    readonly resolveSourceLeaveTerminal: (succeeded: boolean) => void;
  }>();
  private readonly pendingInstanceMaterializations = new Map<
    string,
    Promise<ZLinkSpotActivation>
  >();

  constructor(private readonly options: ZLinkSpotManagerOptions) {
    this.activations = new ZLinkSpotActivationRegistry(options.metrics);
    this.factories = new Set(options.spotFactories);
    this.workerRuntime = options.workerRuntime ?? new ZLinkWorkerRuntime();
    this.locationClaim = new ZLinkSpotLocationClaim({
      lifecycle: options.locationLifecycle,
      nodeRid: options.nodeRid,
      nodeRidProvider: options.nodeRidProvider,
      nodeGenerationProvider: options.nodeGenerationProvider
    });
    this.routedSpotPackets = new ZLinkRoutedSpotPacketDispatch({
      resolveActivation: (spotId) => this.activations.resolveUnique(spotId),
      providerResolver: options.providerResolver,
      dispatchErrors: options.dispatchErrors
    });
    this.actorMembership = new ZLinkSpotActorMembership({
      resolveActivation: (spotId, meshName) => meshName === undefined
        ? this.activations.resolveUnique(spotId)
        : this.activations.resolve(meshName, spotId),
      providerResolver: options.providerResolver,
      messageSerializers: options.messageSerializers,
      dispatchErrors: options.dispatchErrors,
      entrySpotCallbacks: options.entrySpotCallbacks,
      nodeRid: options.nodeRid,
      entryNodeRid: options.entryNodeRid,
      entryNodeRidProvider: options.entryNodeRidProvider,
      actorTransferRuntime: options.actorTransferRuntime
    });
    this.activationLifecycle = new ZLinkSpotActivationLifecycle({
      spotTimerHandlers: options.spotTimerHandlers,
      spotPacketHandlers: options.spotPacketHandlers,
      spotSubscriptionHandlers: options.spotSubscriptionHandlers,
      spotActorSendHandlers: options.spotActorSendHandlers,
      spotActorRequestHandlers: options.spotActorRequestHandlers,
      nodeRid: options.nodeRid,
      nodeRidProvider: options.nodeRidProvider,
      actorCountProvider: options.actorCountProvider,
      userSpotExecutionMode: options.userSpotExecutionMode,
      channelClient: options.channelClient,
      fanoutClient: options.fanoutClient,
      spotPublisherClient: options.spotPublisherClient,
      routedTransport: options.routedTransport,
      addressTransport: options.addressTransport,
      spotRouterChannelIdForMesh: options.spotRouterChannelIdForMesh,
      providerResolver: options.providerResolver,
      dispatchErrors: options.dispatchErrors,
      runtimeEventPublisher: options.runtimeEventPublisher,
      workerRuntime: this.workerRuntime,
      messageSerializers: options.messageSerializers,
      locationClaim: this.locationClaim,
      createNativeSpot: options.createNativeSpot,
      nativeSpotNodeProvider: options.nativeSpotNodeProvider,
      actorResolver: options.actorResolver,
      detachedTaskRunner: options.detachedTaskRunner,
      actorTransferRuntime: options.actorTransferRuntime,
      boundSessionRuntime: options.boundSessionRuntime,
      actorHandoffRuntime: options.actorHandoffRuntime,
      leaveActor: (spotId, actor, signal) => this.actorMembership.leaveActor(spotId, actor, signal),
      closeSpot: (meshName, spotId, signal, reason) =>
        this.closeWithReason(meshName, spotId, signal, reason),
      registerActivation: (activation) => this.activations.register(activation),
      metrics: options.metrics
    });
  }

  materializeInstance(
    meshName: string,
    instanceType: string,
    spotId: RoutingId,
    objectGeneration: bigint,
    signal?: AbortSignal
  ): Promise<void> {
    const current = this.activations.resolve(meshName, spotId);
    if (current !== undefined) {
      if (current.spotType !== this.requireInstanceFactory(meshName, instanceType)) {
        throw new ZLinkConfigurationException(
          `Instance Spot '${String(spotId)}' is assigned to another type.`
        );
      }
      return Promise.resolve();
    }
    const key = `${meshName}\0${String(spotId)}`;
    let pending = this.pendingInstanceMaterializations.get(key);
    if (pending === undefined) {
      pending = this.activationLifecycle.materializeInstance(
        meshName,
        instanceType,
        this.requireInstanceFactory(meshName, instanceType),
        spotId,
        objectGeneration,
        signal
      );
      this.pendingInstanceMaterializations.set(key, pending);
      void pending.finally(() => {
        if (this.pendingInstanceMaterializations.get(key) === pending) {
          this.pendingInstanceMaterializations.delete(key);
        }
      }).catch(() => undefined);
    }
    return pending.then(() => undefined);
  }

  async discardInstance(meshName: string, spotId: RoutingId): Promise<void> {
    const activation = this.activations.resolve(meshName, spotId);
    if (activation === undefined) return;
    activation.requestClose();
    const operation = this.activations.startClose(
      meshName,
      spotId,
      (current) => this.activationLifecycle.discardInstance(current),
      () => true
    );
    await operation?.ready;
  }

  private requireInstanceFactory(
    meshName: string,
    instanceType: string
  ): Type<ZLinkInstanceSpot> {
    const factory = this.options.instanceSpotFactories?.get(meshName)?.get(instanceType);
    if (factory === undefined) {
      throw new ZLinkConfigurationException(
        `Instance Spot factory '${instanceType}' is not registered on RouteMesh '${meshName}'.`
      );
    }
    return factory;
  }

  private isInstanceFactory(
    meshName: string,
    implementation: Type<ZLinkSpot>
  ): boolean {
    return [...(this.options.instanceSpotFactories?.get(meshName)?.values() ?? [])]
      .some(factory => factory === implementation);
  }

  async create<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async create<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async create<TSpot extends ZLinkSpot, TRequest>(
    meshName: string,
    spotType: Type<TSpot>,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async create<TSpot extends ZLinkSpot, TRequest>(
    meshName: string,
    spotType: Type<TSpot>,
    requestOrSignal?: ZLinkMessage | TRequest | AbortSignal,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult> {
    requireMeshName(meshName);
    const args = normalizeSpotCreateArgs(requestOrSignal, signal);
    this.options.admission?.requireRequest('SPOT create', meshName);
    const spotId = this.activations.allocateSpotId(meshName);
    const ownedRequest = args.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
    try {
      return await this.createActivation(meshName, spotType, spotId, ownedRequest, args.signal);
    } finally {
      ownedRequest.close();
    }
  }

  async getOrCreate<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot, TRequest>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot, TRequest>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    requestOrSignal?: ZLinkMessage | TRequest | AbortSignal,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult> {
    return await this.getOrCreateWithAuthority(
      meshName,
      spotType,
      spotId,
      requestOrSignal,
      undefined,
      signal
    );
  }

  async getOrCreateWithAuthority<TSpot extends ZLinkSpot, TRequest>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    requestOrSignal: ZLinkMessage | TRequest | AbortSignal | undefined,
    authority: ZLinkNativeSpotAuthority | undefined,
    signal?: AbortSignal
  ): Promise<ZLinkLocalSpotCreateResult> {
    requireMeshName(meshName);
    const args = normalizeSpotCreateArgs(requestOrSignal, signal);
    throwIfAborted(args.signal);
    const operation = this.activations.getOrBegin(meshName, spotType, spotId, async () => {
      this.options.admission?.requireRequest('SPOT create', meshName);
      const ownedRequest = args.request === undefined
        ? BindingMessage.from(Buffer.alloc(0))
        : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
      try {
        return await this.createActivation(
          meshName,
          spotType,
          spotId,
          ownedRequest,
          undefined,
          authority
        );
      } finally {
        ownedRequest.close();
      }
    });
    return await awaitWithAbort(operation.ready, args.signal);
  }

  async find(meshName: string, spotId: RoutingId): Promise<ZLinkSpotInfo | null> {
    requireMeshName(meshName);
    return this.activations.has(meshName, spotId) ? { spotId } : null;
  }

  async list(meshName: string): Promise<readonly ZLinkSpotInfo[]> {
    requireMeshName(meshName);
    return this.activations.list(meshName);
  }

  async drainForShutdown(meshName: string, signal?: AbortSignal): Promise<void> {
    for (const activation of this.activations.activeActivations()) {
      if (activation.meshName !== meshName) continue;
      activation.requestDrainClose(ZLinkSpotCloseReason.HostShutdown);
    }
    await this.activations.whenMeshEmpty(meshName, signal);
  }

  async close(meshName: string, spotId: RoutingId, signal?: AbortSignal): Promise<boolean> {
    return await this.closeWithReason(
      meshName,
      spotId,
      signal,
      ZLinkSpotCloseReason.ExplicitClose
    );
  }

  private async closeWithReason(
    meshName: string,
    spotId: RoutingId,
    signal?: AbortSignal,
    reason = ZLinkSpotCloseReason.ExplicitClose
  ): Promise<boolean> {
    requireMeshName(meshName);
    const closing = this.activations.closingOperation(meshName, spotId);
    if (closing !== undefined) {
      await closing.ready;
      return true;
    }
    const activation = this.activations.resolve(meshName, spotId);
    if (activation === undefined) {
      return false;
    }
    const currentTurn = activation.serial.isCurrentTurn;
    const beginClose = () => {
      const seal = activation.sealExecution();
      const operation = this.activations.startClose(
        meshName,
        spotId,
        (target) => this.activationLifecycle.closeAfterSeal(target, seal, signal, reason),
        (target) => this.activationLifecycle.resourcesReleased(target)
      );
      if (operation === undefined) {
        activation.abortExecutionSeal(seal);
      }
      return operation;
    };
    const operation = beginClose();
    if (operation === undefined) {
      return false;
    }
    if (currentTurn) {
      if (operation.started) {
        this.options.detachedTaskRunner?.runDetached(
          `spot close ${String(spotId)}`,
          () => operation.ready
        );
        if (this.options.detachedTaskRunner === undefined) {
          void operation.ready.catch(() => undefined);
        }
      }
      return true;
    }
    await operation.ready;
    return true;
  }

  async executeOnSpot<TSpot extends ZLinkSpot, TResult>(
    spotType: Type<TSpot>,
    spotId: RoutingId,
    operation: (spot: TSpot) => Promise<TResult> | TResult,
    signal?: AbortSignal
  ): Promise<TResult> {
    throwIfAborted(signal);
    const activation = this.activations.resolveUnique(spotId);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotId}' is not active.`);
    }
    if (activation.spotType !== spotType) {
      throw new ZLinkConfigurationException(`Spot '${spotId}' has a different spot type.`);
    }
    return activation.serial.execute(() => operation(activation.spot as TSpot));
  }

  hasActiveSpot(spotId: RoutingId): boolean {
    return this.activations.resolveUnique(spotId) !== undefined;
  }

  canCloseUserSpot(meshName: string, spotId: RoutingId): boolean {
    return this.activations.canClose(meshName, spotId);
  }

  beginUserSpotPublication(meshName: string, spotId: RoutingId): void {
    this.activations.stage(meshName, spotId);
  }

  publishUserSpot(meshName: string, spotId: RoutingId): void {
    this.activations.publish(meshName, spotId);
  }

  abortUserSpotPublication(meshName: string, spotId: RoutingId): void {
    this.activations.abandonStage(meshName, spotId);
  }

  resolveLocalSpotRoute(spotId: RoutingId): ZLinkSpotRouteTarget | undefined {
    const activation = this.activations.resolveUnique(spotId);
    const generation = activation?.nativeSpot?.lifecycleGeneration;
    if (activation === undefined || generation === undefined || generation <= 0n) {
      return undefined;
    }
    const nodeRid = this.options.nodeRidProvider?.(activation.meshName) ?? this.options.nodeRid;
    if (nodeRid === undefined) {
      return undefined;
    }
    return {
      routerChannelId: this.options.spotRouterChannelIdForMesh?.(activation.meshName) ?? activation.meshName,
      targetNodeRid: nodeRid,
      spotId: activation.spotId,
      spotKind: ZLinkSpotKind.User,
      targetSpotGeneration: generation
    };
  }

  async admitActorJoin(
    spotId: RoutingId,
    actor: ZLinkActor,
    request: Message,
    commit: (spot: ZLinkSpot) => Promise<ZLinkActorJoinRollback | void> | ZLinkActorJoinRollback | void,
    signal?: AbortSignal,
    leaveSource?: () => Promise<void>
  ): Promise<ZLinkSpotActorJoinResponse> {
    const meshName = this.activations.resolveUnique(spotId)?.meshName;
    this.options.admission?.requireRequest('Actor join admission', meshName);
    return await this.actorMembership.admitActorJoin(
      spotId,
      actor,
      request,
      commit,
      signal,
      leaveSource
    );
  }

  async leaveActor(
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.leaveActor(spotId, actor, signal);
  }

  async leaveActorInMesh(
    meshName: string,
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    requireMeshName(meshName);
    await this.actorMembership.leaveActor(spotId, actor, signal, meshName);
  }

  async notifyActorLeftAfterTransfer(
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.notifyActorLeftAfterTransfer(spotId, actor, signal);
  }

  async prepareActorLeaveForTransfer(
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.prepareActorLeaveForTransfer(spotId, actor, signal);
  }

  async commitActorLeaveAfterTransfer(spotId: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.commitActorLeaveAfterTransfer(spotId, actorId);
  }

  async restoreActorAfterFailedTransfer(
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.restoreActorAfterFailedTransfer(spotId, actor, signal);
  }

  async beginActorTransfer(spotId: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.beginActorTransfer(spotId, actorId);
  }

  async cancelActorTransfer(spotId: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.cancelActorTransfer(spotId, actorId);
  }

  async notifyJoinedSpotActorDisconnected(
    spotId: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<boolean> {
    return await this.actorMembership.notifyJoinedActorDisconnected(spotId, actor, signal);
  }

  dispatchRoutedActorPacket(
    spotId: RoutingId,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    const activation = this.activations.resolveUnique(spotId);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotId}' is not active.`);
    }
    return this.dispatchActorPacket(
      activation,
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
    );
  }

  async dispatchRoutedSpotSend(
    spotId: RoutingId,
    packetName: string | undefined,
    message: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<void> {
    await this.routedSpotPackets.send(spotId, packetName, message, context);
  }

  async dispatchRoutedSpotRequest<TReply>(
    spotId: RoutingId,
    packetName: string | undefined,
    request: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<TReply> {
    return await this.routedSpotPackets.request<TReply>(spotId, packetName, request, context);
  }

  async dispatchMeshSpot(meshName: string, owner: ReadyRecord, record: ReceiveRecord): Promise<void> {
    const spotId = owner.spotId as unknown as RoutingId | null;
    if (spotId === null) {
      throw new ZLinkConfigurationException('MeshNode Spot record is missing the owner Spot RID.');
    }
    if (record.kind === ReceiveKind.SpotMulticast) {
      const activation = this.activations.resolve(meshName, spotId);
      if (activation?.actorDispatch === undefined) {
        throw new ZLinkConfigurationException(
          `MeshNode Spot multicast target '${String(spotId)}' is not active.`
        );
      }
      if (record.topic === null || record.topic.length === 0) {
        throw new ZLinkConfigurationException('MeshNode Spot multicast record is missing its topic.');
      }
      await activation.actorDispatch.dispatchSubscriptionRecord(
        record.topic,
        record.parts,
        record.sourceNodeRid as unknown as RoutingId | null
      );
      return;
    }
    const envelope = decodeChannelEnvelope(record.parts);
    const packetName = envelope.packetName;
    const payload = decodeChannelPayload(
      envelope,
      this.options.messageSerializers === undefined
        ? undefined
        : { serializers: this.options.messageSerializers }
    );
    const context = {
      channelName: envelope.header.channelName,
      contentType: envelope.header.contentType
    };
    if (record.kind === ReceiveKind.SpotSend) {
      await this.routedSpotPackets.send(spotId, packetName, payload, context);
      return;
    }
    if (record.kind !== ReceiveKind.SpotRequest) {
      throw new ZLinkConfigurationException(`Unsupported MeshNode Spot record kind '${record.kind}'.`);
    }
    try {
      const response = await this.routedSpotPackets.request(spotId, packetName, payload, context);
      requireMeshSpotReply(record.reply(encodeChannelReplyParts(envelope.header, response)));
    } catch (error) {
      requireMeshSpotReply(record.reply(encodeChannelErrorReplyParts(
        envelope.header,
        error instanceof Error ? error.message : String(error)
      )));
    }
  }

  async dispatchMeshInstance(
    meshName: string,
    owner: ReadyRecord,
    record: ReceiveRecord
  ): Promise<void> {
    const spotId = owner.spotId as unknown as RoutingId | null;
    if (spotId === null) {
      throw new ZLinkConfigurationException(
        'MeshNode Instance Spot record is missing the owner Spot RID.'
      );
    }
    const activation = this.activations.resolve(meshName, spotId);
    if (activation === undefined || !this.isInstanceFactory(meshName, activation.spotType)) {
      throw new ZLinkConfigurationException(
        `MeshNode Instance Spot target '${String(spotId)}' is not active.`
      );
    }
    const envelope = decodeChannelEnvelope(record.parts);
    const payload = decodeChannelPayload(
      envelope,
      this.options.messageSerializers === undefined
        ? undefined
        : { serializers: this.options.messageSerializers }
    );
    const context = {
      channelName: envelope.header.channelName,
      contentType: envelope.header.contentType
    };
    if (record.operationKind !== OperationKind.InstanceSpotRequest) {
      await this.routedSpotPackets.send(spotId, envelope.packetName, payload, context);
      return;
    }
    try {
      const response = await this.routedSpotPackets.request(
        spotId,
        envelope.packetName,
        payload,
        context
      );
      requireMeshSpotReply(record.reply(encodeChannelReplyParts(envelope.header, response)));
    } catch (error) {
      requireMeshSpotReply(record.reply(encodeChannelErrorReplyParts(
        envelope.header,
        error instanceof Error ? error.message : String(error)
      )));
    }
  }

  async dispatchMeshActor(meshName: string, owner: ReadyRecord, record: ReceiveRecord): Promise<void> {
    const spotId = owner.spotId as unknown as RoutingId | null;
    const entrySpotId = this.options.entryNodeRidProvider?.() ?? this.options.entryNodeRid;
    const targetsEntrySpot = spotId === null
      || (entrySpotId !== undefined && spotId === entrySpotId);
    const actor = owner.actor;
    if (actor === null) {
      throw new ZLinkConfigurationException('MeshNode Actor record is missing its Actor owner.');
    }
    const request = record.kind === ReceiveKind.ActorRequest;
    if (record.sourceBindingGeneration > 0n) {
      this.options.actorBindingGenerationObserver?.(actor.actorId, record.sourceBindingGeneration);
    }
    const resolvedOwner = this.options.actorDispatchOwnerResolver?.(actor.actorId);
    const resolvedActorRef = resolvedOwner?.actorRef ?? {
      nodeRid: String(actor.nodeRid),
      actorId: actor.actorId,
      generation: actor.generation
    };
    const responseActorRef = record.sourceBindingGeneration > 0n
      ? {
          ...resolvedActorRef,
          bindingGeneration: record.sourceBindingGeneration
        } as ActorRef
      : resolvedActorRef;
    const resolvedSpotId = resolvedOwner?.spotId ?? spotId ?? undefined;
    const ownerActorRef = resolvedSpotId === undefined
      ? responseActorRef
      : {
        ...responseActorRef,
        handoffForwarded: true,
        handoffTargetSpotId: String(resolvedSpotId)
      } as ActorRef;
    try {
      if (this.formalRemoteTransfers.has(actor.actorId)) {
        throw new Error(
          `Actor '${actor.actorId}' target transfer reconciliation is not complete.`
        );
      }
      const response = targetsEntrySpot
        ? await this.options.dispatchEntryActorPacket?.(
          actor.actorId,
          record.parts,
          request,
          undefined,
          ownerActorRef
        )
        : await this.dispatchMeshActorPacket(
          meshName,
          spotId,
          actor.actorId,
          record.parts,
          request,
          ownerActorRef
        );
      if (targetsEntrySpot && this.options.dispatchEntryActorPacket === undefined) {
        throw new ZLinkConfigurationException('MeshNode Entry Spot Actor dispatch is not configured.');
      }
      if (request) {
        requireMeshSpotReply(record.reply(this.encodeMeshActorReply(
          record.parts[0],
          ZLinkStreamMessageKind.Response,
          response
        )));
      }
    } catch (error) {
      if (!request) {
        throw error;
      }
      requireMeshSpotReply(record.reply(this.encodeMeshActorReply(
        record.parts[0],
        ZLinkStreamMessageKind.Error,
        {
          message: error instanceof Error ? error.message : String(error),
          kind: 'RequestFailed',
          isRetriable: false
        }
      )));
    }
  }

  async dispatchMeshActorJoin(meshName: string, owner: ReadyRecord, record: ReceiveRecord): Promise<void> {
    const spotId = owner.spotId as unknown as RoutingId | null;
    const control = record.kindData;
    if (control?.kind !== 'actorControl') {
      throw new ZLinkConfigurationException('MeshNode Actor join record is missing lifecycle identity data.');
    }
    const actorId = control.currentActor?.actorId;
    if (spotId === null || actorId === undefined) {
      throw new ZLinkConfigurationException('MeshNode Actor join record is missing its Spot or Actor owner.');
    }
    const activation = this.activations.resolve(meshName, spotId);
    const transferRequest = record.parts.length === 0
      ? undefined
      : decodeFormalRemoteTransferRequest(record.parts[0]!);
    const applicationClaim = transferRequest === undefined
      ? this.options.admission?.claim(meshName, 'Actor join dispatch')
      : undefined;
    let actor = this.options.actorResolver?.(actorId);
    let callbackRequest: BindingMessage | undefined = record.parts.length === 0
      ? undefined
      : record.parts[0]!;
    let ownedCallbackRequest: BindingMessage | undefined;
    let materialized = false;
    let reply: Message | undefined;
    let deferredJoinRoot: ZLinkDeferredJoinAcceptedRoot | undefined;
    try {
      if (transferRequest !== undefined) {
        ownedCallbackRequest = BindingMessage.from(
          Buffer.from(transferRequest.request, 'base64')
        );
        callbackRequest = ownedCallbackRequest;
      }
      let accepted = false;
      if (
        activation !== undefined
        && callbackRequest !== undefined
        && (actor !== undefined || transferRequest !== undefined)
      ) {
        const request = callbackRequest;
        const rawActorRef = transferRequest?.actorRef ?? control.currentActor ?? undefined;
        if (rawActorRef === undefined) {
          throw new ZLinkConfigurationException(`Actor '${actorId}' join record has no ActorRef.`);
        }
        const actorRef: ActorRef = {
          nodeRid: rawActorRef.nodeRid as unknown as RoutingId,
          actorId: rawActorRef.actorId,
          generation: rawActorRef.generation
        };
        const joinRequest: ZLinkActorJoinRequest = actor === undefined
          ? Object.freeze({
              actor: Object.freeze({ ...actorRef }),
              actorType: transferRequest!.actorType,
              expectedMembershipEpoch: transferRequest!.expectedMembershipEpoch
            })
          : createActorJoinRequest(
              actor,
              actorRef as ActorRef,
              transferRequest?.expectedMembershipEpoch ?? control.previousMembershipEpoch
            );
        const response: ZLinkSpotActorJoinResponse = await activation.serial.execute(async () =>
          activation.spot.onActorJoin(
            joinRequest,
            wrapFrameworkPayloadMessage(request, this.options.messageSerializers)
          )
        );
        accepted = response.accepted;
        reply = response.reply === undefined
          ? undefined
          : encodeFrameworkPayloadMessage(response.reply, this.options.messageSerializers);
        if (
          accepted
          && transferRequest?.completionOperationId !== undefined
          && this.options.actorTransferRuntime !== undefined
        ) {
          deferredJoinRoot = await this.options.actorTransferRuntime
            .prepareDeferredJoinAccepted(
              actorId,
              transferRequest.completionOperationId,
              actorRef,
              reply?.data() ?? Buffer.alloc(0)
            );
        }
      }
      if (
        accepted
        && actor === undefined
        && transferRequest !== undefined
        && this.options.actorTransferRuntime !== undefined
      ) {
        const transferState = BindingMessage.from(
          Buffer.from(transferRequest.transferState, 'base64')
        );
        try {
          const result = await this.options.actorTransferRuntime.materializeRoutedActor(
            actorId,
            transferRequest.actorType,
            transferRequest.transferAdapterKey,
            transferState,
            transferRequest.actorEntryNodeRid,
            transferRequest.remoteBoundSessionTarget
          );
          actor = result.actor;
          materialized = true;
        } finally {
          transferState.close();
        }
      }
      if (accepted && actor !== undefined && transferRequest !== undefined) {
        const sourceLeave = sourceLeaveTerminal();
        this.formalRemoteTransfers.set(actorId, {
          actor,
          spotId,
          transferId: transferRequest.transferId,
          handoffBacklog: transferRequest.handoffBacklog,
          deferredJoinRoot,
          sourceLeaveTerminal: sourceLeave.promise,
          resolveSourceLeaveTerminal: sourceLeave.resolve
        });
      }
      requireMeshSpotReply(record.replyActorJoin(
        accepted ? 0 : 1,
        reply === undefined ? [] : [reply.data()]
      ));
    } catch (error) {
      if (materialized && actor !== undefined) {
        await this.options.actorTransferRuntime?.rollbackRoutedActor(actor);
      }
      throw error;
    } finally {
      ownedCallbackRequest?.close();
      reply?.close();
      applicationClaim?.close();
    }
  }

  async dispatchMeshSpotControl(meshName: string, owner: ReadyRecord, record: ReceiveRecord): Promise<void> {
    const spotId = owner.spotId as unknown as RoutingId | null;
    const control = record.kindData;
    if (control?.kind !== 'actorControl') {
      throw new ZLinkConfigurationException('MeshNode Spot control record is missing actor lifecycle data.');
    }
    const actorRef = control.lifecycleKind === ActorLifecycleKind.Left
      ? control.previousActor
      : control.currentActor;
    const actorId = actorRef?.actorId;
    const pendingTransfer = actorId === undefined
      ? undefined
      : this.formalRemoteTransfers.get(actorId);
    const actor = actorId === undefined
      ? undefined
      : pendingTransfer?.actor
        ?? (spotId === null ? undefined : this.activations.resolve(meshName, spotId)?.resolveJoinedActor(actorId))
        ?? this.options.actorLifecycleResolver?.(actorId)
        ?? this.options.actorResolver?.(actorId);
    const entrySpotId = this.options.entryNodeRidProvider?.() ?? this.options.entryNodeRid;
    if (actor === undefined) {
      return;
    }
    if (control.lifecycleKind === ActorLifecycleKind.Left
      && (spotId === null || (entrySpotId !== undefined && String(spotId) === String(entrySpotId)))) {
      const callback = async () => {
        await this.options.entrySpotCallbacks?.onLeaveActor(
          actor,
          undefined,
          actorRef as unknown as ActorRef,
          control.previousMembershipEpoch
        );
      };
      if (this.options.actorTransferRuntime === undefined) {
        await callback();
      } else {
        await this.options.actorTransferRuntime.notifyCoreSourceLeave(actor, callback);
      }
      return;
    }
    if (spotId === null) {
      return;
    }
    const activation = this.activations.resolve(meshName, spotId);
    if (activation === undefined) {
      return;
    }
    await activation.serial.execute(async () => {
      if (control.lifecycleKind === ActorLifecycleKind.Joined) {
        if (control.currentActor !== null) {
          this.options.actorTransferRuntime?.bindRoutedActorRef(
            actor,
            control.currentActor as unknown as ActorRef
          );
        }
        this.options.actorTransferRuntime?.commitRoutedActor(actor, spotId, activation.spot);
        await activation.spot.onJoinedActor(createActorMembership(
          actor,
          control.currentActor as unknown as ActorRef,
          control.currentMembershipEpoch
        ));
        const completeTargetCommit = async (sourceLeaveSucceeded: boolean): Promise<void> => {
          if (sourceLeaveSucceeded && pendingTransfer !== undefined) {
            await replayActorHandoffBacklog(
              pendingTransfer.handoffBacklog,
              (parts, returnResponse, remoteBoundSessionTarget, _fallbackActorRef) =>
                this.dispatchActorPacket(
                  activation,
                  actor.context.actorId,
                  parts,
                  returnResponse,
                  remoteBoundSessionTarget,
                  undefined
                ),
              (index) => this.options.runtimeEventPublisher?.publish({
                sourceName: 'zlink.framework.actor-handoff',
                timestamp: new Date(),
                marker: 'backlog_enqueued',
                actorId: actor.context.actorId,
                index
              })
            );
          }
          await this.options.actorTransferRuntime?.claimRoutedActorLocation(
            actor,
            spotId,
            meshName,
            {
              spotGeneration: control.currentSpotGeneration,
              membershipEpoch: control.currentMembershipEpoch
            }
          );
          await this.options.actorTransferRuntime?.publishRoutedActorOwnership(actor);
          if (!sourceLeaveSucceeded) {
            throw new Error(`Actor '${actor.context.actorId}' source leave callback failed after target commit.`);
          }
          activation.commitActorJoin(actor);
          const deferredJoinRoot = pendingTransfer?.deferredJoinRoot
            ?? await this.options.actorTransferRuntime?.recoverDeferredJoinAccepted(actor.context.actorId);
          if (deferredJoinRoot !== undefined) {
            const currentRef = this.options.actorTransferRuntime === undefined
              ? null
              : control.currentActor as unknown as ActorRef | null;
            if (currentRef === null) {
              throw new Error(`Actor '${actor.context.actorId}' has no target ref for deferred Join completion.`);
            }
            await this.options.actorTransferRuntime?.commitAndDeliverDeferredJoinAccepted(
              deferredJoinRoot,
              actor,
              currentRef,
              operation => activation.executeActor(
                actor.context.actorId,
                async () => await operation()
              )
            );
          }
          this.formalRemoteTransfers.delete(actor.context.actorId);
        };
        if (pendingTransfer !== undefined) {
          const resume = async (): Promise<void> => {
            const sourceLeaveSucceeded = await pendingTransfer.sourceLeaveTerminal;
            await activation.serial.execute(() =>
              completeTargetCommit(sourceLeaveSucceeded)
            );
          };
          this.options.detachedTaskRunner?.runDetached(
            `actor transfer target commit ${actor.context.actorId}`,
            resume
          );
          if (this.options.detachedTaskRunner === undefined) {
            void resume().catch(() => undefined);
          }
          return;
        }
        await completeTargetCommit(true);
        return;
      }
      if (control.lifecycleKind === ActorLifecycleKind.Left) {
        if (this.options.actorTransferRuntime === undefined) {
          await activation.spot.onLeaveActor(createActorMembership(
            actor,
            control.previousActor as unknown as ActorRef,
            control.previousMembershipEpoch
          ));
        } else {
          await this.options.actorTransferRuntime.notifyCoreSourceLeave(
            actor,
            () => activation.spot.onLeaveActor(createActorMembership(
              actor,
              control.previousActor as unknown as ActorRef,
              control.previousMembershipEpoch
            ))
          );
        }
        activation.commitActorDeparture(actor.context.actorId);
        return;
      }
      if (control.lifecycleKind === ActorLifecycleKind.Disconnected) {
        await activation.spot.onDisconnectActor(createActorMembership(
          actor,
          control.currentActor as unknown as ActorRef,
          control.currentMembershipEpoch
        ));
      }
    });
  }

  completeFormalSourceLeaveTerminal(
    actorId: string,
    transferId: string,
    succeeded: boolean
  ): boolean {
    const pending = this.formalRemoteTransfers.get(actorId);
    if (pending === undefined || pending.transferId !== transferId) {
      return false;
    }
    pending.resolveSourceLeaveTerminal(succeeded);
    return true;
  }

  private encodeMeshActorReply(
    requestHeaderPart: Message,
    kind: ZLinkStreamMessageKind.Response | ZLinkStreamMessageKind.Error,
    payload: unknown
  ): readonly Buffer[] {
    const requestHeader = decodeStreamHeader(requestHeaderPart.data());
    const payloadPart = encodeFrameworkPayloadMessage(payload, this.options.messageSerializers);
    try {
      return [
        Buffer.from(encodeStreamHeader({
          kind,
          codec: ZLinkStreamCodec.Json,
          flags: ZLinkStreamHeaderFlags.None,
          requestSeq: requestHeader.requestSeq,
          name: '',
          metadata: new Map(),
          correlationId: requestHeader.correlationId
        })),
        Buffer.from(payloadPart.data())
      ];
    } finally {
      payloadPart.close();
    }
  }

  private async createActivation<TSpot extends ZLinkSpot>(
    meshName: string,
    spotType: Type<TSpot>,
    spotId: RoutingId,
    request: Message,
    signal?: AbortSignal,
    authority?: ZLinkNativeSpotAuthority
  ): Promise<ZLinkLocalSpotCreateResult> {
    this.requireRegisteredFactory(spotType);
    return await this.activationLifecycle.create(
      meshName,
      spotType,
      spotId,
      request,
      signal,
      authority
    );
  }

  private requireRegisteredFactory(spotType: Type<ZLinkSpot>): void {
    if (!this.factories.has(spotType)) {
      throw new ZLinkConfigurationException('Spot type is not registered as a spot factory.');
    }
  }

  private async dispatchActorPacket(
    activation: ZLinkSpotActivation,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    return await this.activationLifecycle.dispatchActorPacket(
      activation,
      actorId,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef
    );
  }

  private async dispatchMeshActorPacket(
    meshName: string,
    spotId: RoutingId,
    actorId: string,
    parts: readonly Message[],
    returnResponse: boolean,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> {
    const activation = this.activations.resolve(meshName, spotId);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(
        `Spot '${String(spotId)}' is not active in mesh '${meshName}'.`
      );
    }
    return await this.dispatchActorPacket(
      activation,
      actorId,
      parts,
      returnResponse,
      undefined,
      fallbackActorRef
    );
  }

}

interface ZLinkFormalRemoteTransferRequest {
  readonly actorType: string;
  readonly transferId: string;
  readonly transferAdapterKey?: string;
  readonly transferState: string;
  readonly request: string;
  readonly actorEntryNodeRid?: RoutingId;
  readonly actorRef?: ActorRef;
  readonly remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget;
  readonly expectedMembershipEpoch: bigint;
  readonly handoffBacklog: readonly ZLinkActorHandoffPacket[];
  readonly completionOperationId?: {
    readonly high: bigint;
    readonly low: bigint;
  };
}

function decodeFormalRemoteTransferRequest(
  message: Message
): ZLinkFormalRemoteTransferRequest | undefined {
  let payload: ZLinkRemoteActorJoinWirePayload;
  try {
    payload = JSON.parse(message.data().toString()) as ZLinkRemoteActorJoinWirePayload;
  } catch {
    return undefined;
  }
  if (
    payload.packetName !== REMOTE_ACTOR_JOIN_PACKET
    || typeof payload.actorType !== 'string'
    || typeof payload.transferId !== 'string'
    || typeof payload.transferState !== 'string'
    || typeof payload.request !== 'string'
  ) {
    return undefined;
  }
  return {
    actorType: payload.actorType,
    transferId: payload.transferId,
    transferAdapterKey: typeof payload.transferAdapterKey === 'string'
      ? payload.transferAdapterKey
      : undefined,
    transferState: payload.transferState,
    request: payload.request,
    actorEntryNodeRid: typeof payload.actorEntryNodeRid === 'string'
      ? decodeRoutingId(payload.actorEntryNodeRid, payload.actorEntryNodeRidHex)
      : undefined,
    actorRef: decodeRemoteActorRef(
      payload.actorNodeRid,
      payload.actorNodeRidHex,
      typeof payload.actorId === 'string' ? payload.actorId : '',
      payload.actorGeneration
    ) as ActorRef | undefined,
    remoteBoundSessionTarget: decodeRemoteBoundSessionTarget(
      payload.boundSessionRouterChannelId,
      payload.boundSessionTargetNodeRid,
      payload.boundSessionTargetNodeRidHex,
      payload.boundSessionSpotId,
      payload.boundSessionNodeRid,
      payload.boundSessionNodeRidHex,
      payload.boundSessionRid,
      payload.boundSessionRidHex,
      payload.boundSessionBindingGeneration
    ),
    expectedMembershipEpoch: typeof payload.expectedMembershipEpoch === 'string'
      ? BigInt(payload.expectedMembershipEpoch)
      : 0n,
    handoffBacklog: decodeHandoffBacklog(payload.handoffBacklog),
    completionOperationId:
      typeof payload.completionOperationHigh === 'string'
      && typeof payload.completionOperationLow === 'string'
        ? {
            high: BigInt(payload.completionOperationHigh),
            low: BigInt(payload.completionOperationLow)
          }
        : undefined
  };
}

function sourceLeaveTerminal(): {
  readonly promise: Promise<boolean>;
  readonly resolve: (succeeded: boolean) => void;
} {
  let resolve!: (succeeded: boolean) => void;
  const promise = new Promise<boolean>((accept) => {
    resolve = accept;
  });
  return { promise, resolve };
}

function requireMeshSpotReply(result: number): void {
  if (result !== SubmitResult.Ok) {
    throw new ZLinkConfigurationException(
      `MeshNode reply was not accepted (submit result ${result}).`
    );
  }
}

function normalizeSpotCreateArgs<TRequest>(
  requestOrSignal: ZLinkMessage | TRequest | AbortSignal | undefined,
  signal: AbortSignal | undefined
): { readonly request: ZLinkMessage | TRequest | undefined; readonly signal: AbortSignal | undefined } {
  if (isAbortSignal(requestOrSignal)) {
    return { request: undefined, signal: requestOrSignal };
  }
  return { request: requestOrSignal, signal };
}

function requireMeshName(meshName: string): void {
  if (meshName.length === 0) {
    throw new ZLinkConfigurationException('Spot operations require a mesh name.');
  }
}

function isAbortSignal(value: unknown): value is AbortSignal {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { aborted?: unknown }).aborted === 'boolean'
    && typeof (value as { addEventListener?: unknown }).addEventListener === 'function';
}
