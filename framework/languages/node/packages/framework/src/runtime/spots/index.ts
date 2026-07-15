import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkMessageSerializer,
  ZLinkActor,
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkProviderResolver,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotCreateResult,
  ZLinkSpotInfo,
  ZLinkSpotManager,
  ZLinkSpotPublisherClient,
  ZLinkSpotDrainPolicy,
} from '../../contracts';
import type {
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotTimerHandlerRegistration
} from '../../contracts/Configuration/RegistrationTypes';
export { createSpotHandle, resolveSpotHandle } from './spot-handle';
import type { ZLinkSpotRouteResolver } from './spot-routing-internal';
import type { Message } from '../../contracts/Common/Message';
import { awaitWithAbort, throwIfAborted } from '../abort';
import {
  ZLinkMessage,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import {
  Message as BindingMessage
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException
} from '../configuration';
import type {
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';

import { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkWorkerRuntime } from '../workers';
import type { ZLinkRemoteBoundSessionTarget } from '../actors';
import type { ZLinkLocationLifecycle } from '../locations';
import {
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
export { ZLinkSpotSerialExecutor } from './spot-serial-executor';
export {
  ZLinkManagedTimer,
  ZLinkSpotTimerRegistry
} from './spot-timer';
export {
  DefaultZLinkSpotOutbound,
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
import { ZLinkSpotActivationLifecycle } from './spot-activation';
import { ZLinkSpotActorMembership, type ZLinkActorJoinRollback } from './spot-actor-membership';
import type {
  ZLinkSpotActorHandoffRuntime,
  ZLinkSpotActorTransferRuntime,
  ZLinkSpotBoundSessionRuntime
} from './spot-runtime-ports';
import type { ZLinkRuntimeAdmissionGate } from '../admission';
import type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
export type { ZLinkDetachedTaskRunner } from './spot-actor-join-dispatch';
import { ZLinkSpotLocationClaim } from './spot-location-claim';
import { ZLinkRoutedSpotPacketDispatch } from './spot-routed-spot-packet-dispatch';
export { ZLinkEntrySpotActivation } from './spot-entry-activation';
export {
  ZLinkSpotNodeRuntimeManager,
  type ZLinkSpotNodeRuntimeManagerOptions
} from './spot-node-runtime-manager';

export interface ZLinkSpotManagerOptions {
  readonly spotFactories: readonly Type<ZLinkSpot>[];
  readonly spotTimerHandlers?: readonly ZLinkSpotTimerHandlerRegistration[];
  readonly spotPacketHandlers?: readonly ZLinkSpotPacketHandlerRegistration[];
  readonly spotSubscriptionHandlers?: readonly ZLinkSpotSubscriptionHandlerRegistration[];
  readonly spotActorSendHandlers?: readonly ZLinkSpotActorSendHandlerRegistration[];
  readonly spotActorRequestHandlers?: readonly ZLinkSpotActorRequestHandlerRegistration[];
  readonly nodeRid?: RoutingId;
  readonly nodeRidProvider?: () => RoutingId | undefined;
  readonly entryNodeRid?: RoutingId;
  readonly entryNodeRidProvider?: () => RoutingId | undefined;
  readonly entrySpotCallbacks?: {
    onLeaveActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
  };
  readonly actorCountProvider?: (spotRid: RoutingId) => number;
  readonly channelClient?: ZLinkChannelClient;
  readonly fanoutClient?: ZLinkFanoutClient;
  readonly spotPublisherClient?: ZLinkSpotPublisherClient;
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkSpotRoutedTransport;
  readonly spotRouterChannelIdForMesh?: (meshName: string) => string;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly dispatchErrors?: ZLinkDispatchErrorReporter;
  readonly runtimeEventPublisher?: ZLinkRuntimeEventPublisher;
  readonly workerRuntime?: ZLinkWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly locationMeshName?: string;
  // Backs each user Spot with a core-native Spot object (registered for join
  // routing by rid) so actor-join admission uses the same recv/reply round-trip
  // as the Entry Spot and .NET, for local and remote callers alike.
  readonly createNativeSpot?: (spotRid: RoutingId) => ZLinkBackendSpot | undefined;
  readonly nativeSpotNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorResolver?: (actorId: string) => ZLinkActor | undefined;
  readonly detachedTaskRunner?: ZLinkDetachedTaskRunner;
  readonly actorTransferRuntime?: ZLinkSpotActorTransferRuntime;
  readonly boundSessionRuntime?: ZLinkSpotBoundSessionRuntime;
  readonly actorHandoffRuntime?: ZLinkSpotActorHandoffRuntime;
  readonly metrics?: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly spotDrainPolicy?: (spotType: Type<ZLinkSpot>) => ZLinkSpotDrainPolicy;
  readonly admission?: ZLinkRuntimeAdmissionGate;
}

export class DefaultZLinkSpotManager implements ZLinkSpotManager {
  private readonly factories: ReadonlySet<Type<ZLinkSpot>>;
  private readonly activations: ZLinkSpotActivationRegistry;
  private readonly workerRuntime: ZLinkWorkerRuntime;
  private readonly locationClaim: ZLinkSpotLocationClaim;
  private readonly routedSpotPackets: ZLinkRoutedSpotPacketDispatch;
  private readonly actorMembership: ZLinkSpotActorMembership;
  private readonly activationLifecycle: ZLinkSpotActivationLifecycle;

  constructor(private readonly options: ZLinkSpotManagerOptions) {
    this.activations = new ZLinkSpotActivationRegistry(options.metrics);
    this.factories = new Set(options.spotFactories);
    this.workerRuntime = options.workerRuntime ?? new ZLinkWorkerRuntime();
    this.locationClaim = new ZLinkSpotLocationClaim({
      lifecycle: options.locationLifecycle,
      meshName: options.locationMeshName,
      nodeRid: options.nodeRid,
      nodeRidProvider: options.nodeRidProvider
    });
    this.routedSpotPackets = new ZLinkRoutedSpotPacketDispatch({
      resolveActivation: (spotRid) => this.activations.resolve(spotRid),
      providerResolver: options.providerResolver,
      dispatchErrors: options.dispatchErrors
    });
    this.actorMembership = new ZLinkSpotActorMembership({
      resolveActivation: (spotRid) => this.activations.resolve(spotRid),
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
      channelClient: options.channelClient,
      fanoutClient: options.fanoutClient,
      spotPublisherClient: options.spotPublisherClient,
      routedTransport: options.routedTransport,
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
      leaveActor: (spotRid, actor, signal) => this.actorMembership.leaveActor(spotRid, actor, signal),
      closeSpot: (spotRid, signal) => this.close(spotRid, signal),
      registerActivation: (activation) => this.activations.register(activation),
      metrics: options.metrics
    });
  }

  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async create<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async create<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async create<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    requestOrSignal?: ZLinkMessage | TRequest | AbortSignal,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const args = normalizeSpotCreateArgs(requestOrSignal, signal);
    this.options.admission?.requireRequest('SPOT create');
    const spotRid = this.activations.allocateSpotRid();
    const ownedRequest = args.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
    try {
      return await this.createActivation(spotType, spotRid, ownedRequest, args.signal);
    } finally {
      ownedRequest.close();
    }
  }

  async getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: TRequest,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult>;
  async getOrCreate<TSpot extends ZLinkSpot, TRequest>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    requestOrSignal?: ZLinkMessage | TRequest | AbortSignal,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    const args = normalizeSpotCreateArgs(requestOrSignal, signal);
    throwIfAborted(args.signal);
    const operation = this.activations.getOrBegin(spotType, spotRid, async () => {
      this.options.admission?.requireRequest('SPOT create');
      const ownedRequest = args.request === undefined
        ? BindingMessage.from(Buffer.alloc(0))
        : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
      try {
        return await this.createActivation(spotType, spotRid, ownedRequest);
      } finally {
        ownedRequest.close();
      }
    });
    return await awaitWithAbort(operation.ready, args.signal);
  }

  async find(spotRid: RoutingId): Promise<ZLinkSpotInfo | null> {
    return this.activations.has(spotRid) ? { spotRid } : null;
  }

  async list(): Promise<readonly ZLinkSpotInfo[]> {
    return this.activations.list();
  }

  async drainForShutdown(signal?: AbortSignal): Promise<void> {
    for (const activation of this.activations.activeActivations()) {
      const policy = this.options.spotDrainPolicy?.(activation.spotType) ?? 'DrainNatural';
      if (policy !== 'ReleaseAndRecreate') continue;
      activation.requestDrainClose();
    }
    await this.activations.whenEmpty(signal);
  }

  retainsActorDuringDrain(spotRid: RoutingId | undefined): boolean {
    if (spotRid === undefined) return false;
    const activation = this.activations.resolve(spotRid);
    if (activation === undefined) return false;
    return (this.options.spotDrainPolicy?.(activation.spotType) ?? 'DrainNatural') === 'DrainNatural';
  }

  async close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean> {
    const closing = this.activations.closingOperation(spotRid);
    if (closing !== undefined) {
      await closing.ready;
      return true;
    }
    const activation = this.activations.resolve(spotRid);
    if (activation === undefined) {
      return false;
    }
    const currentTurn = activation.serial.isCurrentTurn;
    const beginClose = () => this.activations.startClose(
      spotRid,
      (target) => target.serial.post(() => this.activationLifecycle.closeInsideSerial(target, signal)),
      (target) => this.activationLifecycle.resourcesReleased(target)
    );
    const operation = currentTurn
      ? beginClose()
      : await activation.serial.execute(beginClose);
    if (operation === undefined) {
      return false;
    }
    if (currentTurn) {
      if (operation.started) {
        this.options.detachedTaskRunner?.runDetached(
          `spot close ${String(spotRid)}`,
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
    spotRid: RoutingId,
    operation: (spot: TSpot) => Promise<TResult> | TResult,
    signal?: AbortSignal
  ): Promise<TResult> {
    throwIfAborted(signal);
    const activation = this.activations.resolve(spotRid);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    if (activation.spotType !== spotType) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' has a different spot type.`);
    }
    return activation.serial.execute(() => operation(activation.spot as TSpot));
  }

  hasActiveSpot(spotRid: RoutingId): boolean {
    return this.activations.has(spotRid);
  }

  async admitActorJoin(
    spotRid: RoutingId,
    actor: ZLinkActor,
    request: Message,
    commit: (spot: ZLinkSpot) => Promise<ZLinkActorJoinRollback | void> | ZLinkActorJoinRollback | void,
    signal?: AbortSignal,
    leaveSource?: () => Promise<void>
  ): Promise<ZLinkSpotActorJoinResponse> {
    this.options.admission?.requireRequest('Actor join admission');
    return await this.actorMembership.admitActorJoin(
      spotRid,
      actor,
      request,
      commit,
      signal,
      leaveSource
    );
  }

  async leaveActor(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.leaveActor(spotRid, actor, signal);
  }

  async notifyActorLeftAfterTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.notifyActorLeftAfterTransfer(spotRid, actor, signal);
  }

  async prepareActorLeaveForTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.prepareActorLeaveForTransfer(spotRid, actor, signal);
  }

  async commitActorLeaveAfterTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.commitActorLeaveAfterTransfer(spotRid, actorId);
  }

  async restoreActorAfterFailedTransfer(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.restoreActorAfterFailedTransfer(spotRid, actor, signal);
  }

  async beginActorTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.beginActorTransfer(spotRid, actorId);
  }

  async cancelActorTransfer(spotRid: RoutingId, actorId: string): Promise<void> {
    await this.actorMembership.cancelActorTransfer(spotRid, actorId);
  }

  async notifyJoinedSpotActorDisconnected(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<boolean> {
    return await this.actorMembership.notifyJoinedActorDisconnected(spotRid, actor, signal);
  }

  dispatchRoutedActorPacket(
    spotRid: RoutingId,
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget
  ): Promise<unknown> {
    const activation = this.activations.resolve(spotRid);
    if (activation === undefined) {
      throw new ZLinkConfigurationException(`Spot '${spotRid}' is not active.`);
    }
    return this.dispatchActorPacket(activation, actorId, parts, returnResponse, remoteBoundSessionTarget);
  }

  async dispatchRoutedSpotSend(
    spotRid: RoutingId,
    packetName: string | undefined,
    message: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<void> {
    await this.routedSpotPackets.send(spotRid, packetName, message, context);
  }

  async dispatchRoutedSpotRequest<TReply>(
    spotRid: RoutingId,
    packetName: string | undefined,
    request: unknown,
    context: { readonly channelName: string; readonly contentType?: string }
  ): Promise<TReply> {
    return await this.routedSpotPackets.request<TReply>(spotRid, packetName, request, context);
  }

  private async createActivation<TSpot extends ZLinkSpot>(
    spotType: Type<TSpot>,
    spotRid: RoutingId,
    request: Message,
    signal?: AbortSignal
  ): Promise<ZLinkSpotCreateResult> {
    this.requireRegisteredFactory(spotType);
    return await this.activationLifecycle.create(spotType, spotRid, request, signal);
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


function isAbortSignal(value: unknown): value is AbortSignal {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { aborted?: unknown }).aborted === 'boolean'
    && typeof (value as { addEventListener?: unknown }).addEventListener === 'function';
}
