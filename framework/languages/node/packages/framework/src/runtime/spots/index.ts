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
  ZLinkSpotTimerHandlerRegistration,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotCreateResult,
  ZLinkSpotInfo,
  ZLinkSpotManager,
  ZLinkSpotPublisherClient,
} from '../../contracts';
import type { ZLinkSpotRouteResolver } from './spot-routing-internal';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkMessage,
  isZLinkMessage,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import {
  Message as BindingMessage
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException
} from '../configuration';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendActorJoinInfo
} from '../backend/contracts';

import { ZLinkDispatchErrorReporter } from '../channels';
import { ZLinkSpotWorkerRuntime } from '../workers';
import type { ZLinkRemoteActorPacketTarget, ZLinkRemoteBoundSessionTarget } from '../actors';
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
import type { ZLinkRemoteActorJoinActor } from './spot-remote-codec';
import {
  ZLinkSpotActivationRegistry,
  type ZLinkSpotActivation
} from './spot-activation-registry';
import { ZLinkSpotActivationLifecycle } from './spot-activation';
import { ZLinkSpotActorMembership } from './spot-actor-membership';
import type { ZLinkActorResponseOptions } from './spot-actor-packet-dispatch';
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
  readonly actorEntryNodeRidProvider?: (actor: ZLinkActor) => RoutingId | undefined;
  readonly entrySpotCallbacks?: {
    onJoinedActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void>;
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
  readonly workerRuntime?: ZLinkSpotWorkerRuntime;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly locationMeshName?: string;
  // Backs each user Spot with a core-native Spot object (registered for join
  // routing by rid) so actor-join admission uses the same recv/reply round-trip
  // as the Entry Spot and .NET, for local and remote callers alike.
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
  readonly nativeJoinBoundSessionTargetResolver?: (
    info: ZLinkBackendActorJoinInfo
  ) => ZLinkRemoteBoundSessionTarget | undefined;
  readonly routedActorCommitter?: (actor: ZLinkActor, spotRid: RoutingId, spot: ZLinkSpot) => void;
  readonly routedActorLeaveCommitter?: (actor: ZLinkActor) => void;
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

export class DefaultZLinkSpotManager implements ZLinkSpotManager {
  private readonly factories: ReadonlySet<Type<ZLinkSpot>>;
  private readonly activations = new ZLinkSpotActivationRegistry();
  private readonly workerRuntime: ZLinkSpotWorkerRuntime;
  private readonly locationClaim: ZLinkSpotLocationClaim;
  private readonly routedSpotPackets: ZLinkRoutedSpotPacketDispatch;
  private readonly actorMembership: ZLinkSpotActorMembership;
  private readonly activationLifecycle: ZLinkSpotActivationLifecycle;

  constructor(private readonly options: ZLinkSpotManagerOptions) {
    this.factories = new Set(options.spotFactories);
    this.workerRuntime = options.workerRuntime ?? new ZLinkSpotWorkerRuntime();
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
      actorEntryNodeRidProvider: options.actorEntryNodeRidProvider,
      routedActorLeaveCommitter: options.routedActorLeaveCommitter
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
      routedActorProvider: options.routedActorProvider,
      nativeJoinBoundSessionTargetResolver: options.nativeJoinBoundSessionTargetResolver,
      routedActorCommitter: options.routedActorCommitter,
      actorResponseSender: options.actorResponseSender,
      actorErrorSender: options.actorErrorSender,
      routedBoundSessionReceiver: options.routedBoundSessionReceiver,
      routedBoundSessionResponseReceiver: options.routedBoundSessionResponseReceiver,
      routedBoundSessionErrorReceiver: options.routedBoundSessionErrorReceiver,
      remoteActorPacketTargetReceiver: options.remoteActorPacketTargetReceiver,
      remoteBoundSessionTargetResolver: options.remoteBoundSessionTargetResolver,
      actorPacketTargetProvider: options.actorPacketTargetProvider,
      leaveActor: (spotRid, actor, signal) => this.actorMembership.leaveActor(spotRid, actor, signal),
      closeSpot: (spotRid, signal) => this.close(spotRid, signal),
      registerActivation: (activation) => this.activations.register(activation)
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
    const spotRid = this.activations.allocateSpotRid();
    const ownedRequest = args.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
    try {
      return await this.createActivation(spotType, spotRid, ownedRequest, args.signal);
    } finally {
      if (ownsFrameworkPayloadMessage(args.request)) {
        ownedRequest.close();
      }
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
    const existingOrPending = await this.activations.existingOrPendingResult(spotType, spotRid);
    if (existingOrPending !== undefined) {
      return existingOrPending;
    }

    const ownedRequest = args.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(args.request, this.options.messageSerializers);
    const ready = Promise.resolve().then(() => this.createActivation(spotType, spotRid, ownedRequest, args.signal));
    try {
      return await this.activations.trackPending(spotType, spotRid, ready);
    } finally {
      if (ownsFrameworkPayloadMessage(args.request)) {
        ownedRequest.close();
      }
    }
  }

  async find(spotRid: RoutingId): Promise<ZLinkSpotInfo | null> {
    return this.activations.has(spotRid) ? { spotRid } : null;
  }

  async list(): Promise<readonly ZLinkSpotInfo[]> {
    return this.activations.list();
  }

  async close(spotRid: RoutingId, signal?: AbortSignal): Promise<boolean> {
    const activation = this.activations.takeClosable(spotRid);
    if (activation === undefined) {
      return false;
    }
    if (activation.serial.isCurrentTurn) {
      void activation.serial.post(() => this.activationLifecycle.closeInsideSerial(activation, signal));
      return true;
    }
    await this.activationLifecycle.close(activation, signal);
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
    commit: (spot: ZLinkSpot) => Promise<void> | void,
    signal?: AbortSignal
  ): Promise<ZLinkSpotActorJoinResponse> {
    return await this.actorMembership.admitActorJoin(spotRid, actor, request, commit, signal);
  }

  async leaveActor(
    spotRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    await this.actorMembership.leaveActor(spotRid, actor, signal);
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

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

function isAbortSignal(value: unknown): value is AbortSignal {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { aborted?: unknown }).aborted === 'boolean'
    && typeof (value as { addEventListener?: unknown }).addEventListener === 'function';
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}

function ownsFrameworkPayloadMessage(value: unknown): boolean {
  return value === undefined || !(isMessage(value) || (isZLinkMessage(value) && value.isEncoded()));
}
