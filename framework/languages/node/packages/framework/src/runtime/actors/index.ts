import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkProviderResolver,
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkActorJoinEntrySpotCall,
  ZLinkActorJoinResult,
  ZLinkActorJoinSpotCall,
  ZLinkActorManager,
  ZLinkBoundSession,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorReplyOptions,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler,
  ZLinkSpotRemoteAddress,
  ZLinkSpotRemoteAddressResolver
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkEncodedPayload,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import { Message as BindingMessage, RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import { isZLinkMessage, ZLinkMessage, type ZLinkMessageSerializer } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { captureZLinkSpotSerialTurn, type ZLinkSpotSerialTurn } from '../execution';
import type {
  ZLinkBackendActorJoinEntrySpotResult,
  ZLinkBackendActorJoinResult,
  ZLinkBackendActorRef,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import {
  decodeFrameworkPayloadMessage,
  encodeFrameworkPayloadMessage,
  wrapFrameworkPayloadMessage
} from '../messaging/payload-codec';
import type { ZLinkLocationLifecycle } from '../locations';

export interface ZLinkActorManagerOptions {
  readonly actorFactories: ReadonlyMap<string, Type | ZLinkActorFactory>;
  readonly joinCoordinator?: ZLinkActorJoinCoordinator;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly nativeActorNode?: ZLinkBackendSpotNode;
  readonly nativeActorNodeProvider?: () => ZLinkBackendSpotNode | undefined;
  readonly actorCreatedNodeRidProvider?: () => RoutingId | undefined;
  readonly actorCreatedNotifier?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    createRequest: ZLinkMessage,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorDestroyedCleanup?: (actorId: string) => void;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly boundSessionFactory?: ZLinkActorBoundSessionFactory;
  readonly providerResolver?: ZLinkProviderResolver;
}

export type ZLinkActorBoundSessionFactory = (actorId: string) => ZLinkBoundSession;

export interface ZLinkActorJoinCoordinator {
  joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>>;
  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>>;
}

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendSpotNode;
  readonly remoteAddressResolver?: ZLinkSpotRemoteAddressResolver;
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
}

export interface ZLinkRemoteBoundSessionTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
}

export interface ZLinkRemoteActorPacketTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: RoutingId;
  readonly spotRid: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkActorRoutedJoinTransport {
  canRouteChannel?(routerChannelId: string): boolean;
  canRoutePacketChannel?(routerChannelId: string): boolean;
  send(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  sendToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void>;
  requestRawToSpot?(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]>;
  requestToSpot<TReply = unknown>(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply>;
  requestFromSpotToSpot?<TReply = unknown>(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply>;
  requestRawFromSpotToSpot?(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]>;
}

export const ZLINK_REMOTE_ACTOR_JOIN_PACKET = '__zlink.actor.join_spot.request';
const REMOTE_ACTOR_JOIN_PACKET = ZLINK_REMOTE_ACTOR_JOIN_PACKET;
export const ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET = '__zlink.actor.bound_session.send';
export const ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET = '__zlink.actor.bound_session.response';
export const ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET = '__zlink.actor.bound_session.error';
export const ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET = 'zlink.framework.actor.session_disconnected';
export const ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET = '__zlink.actor.packet.relay';

interface ZLinkRemoteActorJoinRequest {
  readonly packetName: typeof REMOTE_ACTOR_JOIN_PACKET;
  readonly actorId: string;
  readonly actorType: string;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorGeneration: string;
  readonly actorCreateRequest?: string;
  readonly routerChannelId?: string;
  readonly sourceSpotRid?: string;
  readonly sourceSpotRidHex?: string;
  readonly boundSessionRouterChannelId?: string;
  readonly boundSessionTargetNodeRid?: string;
  readonly boundSessionTargetNodeRidHex?: string;
  readonly boundSessionSpotRid?: string;
  readonly boundSessionSpotRidHex?: string;
}

interface ZLinkRemoteActorJoinReply {
  readonly accepted: boolean;
  readonly actorNodeRid: string;
  readonly actorNodeRidHex?: string;
  readonly actorId: string;
  readonly actorGeneration: string;
}

export enum ZLinkActorPacketKind {
  Send = 'send',
  Request = 'request'
}

export interface ZLinkActorPacketDescriptor {
  readonly kind: ZLinkActorPacketKind;
  readonly packetName: string;
  readonly actorType: Type<ZLinkActor>;
  readonly handlerType: Type;
}

interface ZLinkActorCreationOperation {
  readonly task: Promise<ZLinkActor>;
  readonly created: boolean;
}

interface ZLinkActorCreateRequest {
  readonly nativeRequest: Message | undefined;
  readonly callbackRequest: ZLinkMessage;
}

/**
 * Owns actor construction: factory resolution (registered instance or
 * provider-constructed type), context creation, instance creation, actor
 * binding, native ref acquisition, and Entry Spot creation notification.
 * Separated from DefaultZLinkActorManager so the manager stays a thin
 * registry/lifecycle facade and creation policy evolves independently.
 */
class ZLinkActorCreationCoordinator {
  constructor(private readonly options: ZLinkActorManagerOptions) {}

  async createActor(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    createRequest: ZLinkActorCreateRequest,
    claimLocation: boolean,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const lifecycle = this.options.locationLifecycle;
    if (lifecycle !== undefined && claimLocation) {
      const nodeRid = this.resolveLocationNodeRid();
      const activation = await lifecycle.executeActorClaimThenActivate(
        actorType,
        actorId,
        nodeRid,
        async () => {
          this.options.actorDestroyedCleanup?.(actorId);
          state.clearAfterDestroy();
        },
        () => this.createActorAfterClaim(actorId, actorType, state, createRequest, true, signal)
      );
      if (activation.activated !== undefined) {
        state.markLocationOwned();
        return activation.activated;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        activation.existingLocation === undefined
          ? `Actor '${actorId}' location claim was rejected and no live location row was found.`
          : `Actor '${actorId}' is already active on node '${activation.existingLocation.nodeRid}' (location claim conflict).`
      );
    }

    return await this.createActorAfterClaim(actorId, actorType, state, createRequest, claimLocation, signal);
  }

  private async createActorAfterClaim(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    createRequest: ZLinkActorCreateRequest,
    updateLocation: boolean,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const factory = await this.createFactory(actorType);
    const context = state.ensureContext(
      this.options.joinCoordinator,
      this.options.boundSessionFactory,
      this.options.messageSerializers
    );
    const actor = await factory.create(actorId, context, signal);
    try {
      state.bindActor(actor, context);
      const nativeActorNode = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
      if (nativeActorNode !== undefined) {
        const actorRef = state.ensureNativeActorRef(nativeActorNode, createRequest.nativeRequest);
        await this.options.actorCreatedNotifier?.(
          toFrameworkRoutingId(actorRef.nodeRid),
          actor,
          createRequest.callbackRequest,
          signal
        );
        if (updateLocation) {
          await this.options.locationLifecycle?.setActorRef(
            actorType,
            actorId,
            serializeActorRef(toFrameworkRoutingId(actorRef.nodeRid), actorId, actorRef.generation)
          );
        }
      } else {
        const nodeRid = this.options.actorCreatedNodeRidProvider?.();
        if (nodeRid !== undefined) {
          await this.options.actorCreatedNotifier?.(nodeRid, actor, createRequest.callbackRequest, signal);
          if (updateLocation) {
            await this.options.locationLifecycle?.setActorRef(
              actorType,
              actorId,
              serializeActorRef(nodeRid, actorId, 0n)
            );
          }
        }
      }
    } catch (error) {
      state.clearAfterDestroy();
      throw error;
    }
    return actor;
  }

  private resolveLocationNodeRid(): RoutingId {
    const nativeActorNode = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
    const nodeRid = nativeActorNode === undefined
      ? this.options.actorCreatedNodeRidProvider?.()
      : toFrameworkRoutingId(nativeActorNode.routingId);
    if (nodeRid === undefined) {
      throw new ZLinkConfigurationException('Location actor claim requires a node routing id.');
    }
    return nodeRid;
  }

  private async createFactory(actorType: string): Promise<ZLinkActorFactory> {
    const factoryOrType = this.options.actorFactories.get(actorType);
    if (factoryOrType === undefined) {
      throw new ZLinkConfigurationException(`Actor factory '${actorType}' is not registered.`);
    }
    if (typeof factoryOrType === 'function') {
      const type = factoryOrType as Type<ZLinkActorFactory>;
      return await createProviderInstance(type, this.options.providerResolver);
    }
    return factoryOrType;
  }
}

export class DefaultZLinkActorManager implements ZLinkActorManager {
  private readonly states = new Map<string, ZLinkActorRuntimeState>();
  private readonly creation: ZLinkActorCreationCoordinator;

  constructor(private readonly options: ZLinkActorManagerOptions) {
    this.creation = new ZLinkActorCreationCoordinator(options);
  }

  async create(actorId: string, actorType: string, signalOrRequest?: AbortSignal | unknown, signal?: AbortSignal): Promise<ActorRef> {
    const args = normalizeCreateRequestArgs(signalOrRequest, signal);
    const result = await this.createOrGet(actorId, actorType, true, args.request, args.signal);
    return result.actorRef;
  }

  async find(actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined> {
    throwIfAborted(signal);
    const state = this.states.get(actorId);
    if (state?.actor === undefined) {
      return undefined;
    }
    return this.actorRefForState(state);
  }

  async getOrCreate(actorId: string, actorType: string, signalOrRequest?: AbortSignal | unknown, signal?: AbortSignal): Promise<ActorRef> {
    const args = normalizeCreateRequestArgs(signalOrRequest, signal);
    const result = await this.createOrGet(actorId, actorType, false, args.request, args.signal);
    return result.actorRef;
  }

  async findActor(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined> {
    throwIfAborted(signal);
    return this.states.get(actorId)?.actor;
  }

  async getOrCreateActor(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor> {
    const result = await this.createOrGet(actorId, actorType, false, undefined, signal);
    return result.actor;
  }

  async getOrCreateWithNativeRef(
    actorId: string,
    actorType: string,
    actorRef: ZLinkBackendActorRef,
    createRequest?: unknown,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const state = this.getOrCreateState(actorId);
    state.setNativeActorRef(actorRef);
    const result = await this.createOrGet(actorId, actorType, false, createRequest, signal, false);
    return result.actor;
  }

  getState(actorId: string): ZLinkActorRuntimeState | undefined {
    return this.states.get(actorId);
  }

  async destroyActor(
    node: ZLinkBackendSpotNode,
    entryNodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ): Promise<void> {
    const destroySignal = signal;
    throwIfAborted(destroySignal);
    const state = this.states.get(actor.actorId);
    if (state === undefined || state.actor === undefined || state.actor !== actor) {
      return;
    }
    if (state.isJoined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' must leave its current SPOT before destroy.`
      );
    }
    if (state.nativeActorRef === undefined) {
      if (state.actorType !== undefined && state.ownsLocation) {
        await this.options.locationLifecycle?.releaseActor(state.actorType, actor.actorId);
      }
      this.options.actorDestroyedCleanup?.(actor.actorId);
      state.clearAfterDestroy();
      this.states.delete(actor.actorId);
      return;
    }
    const actorRef = state.beginDestroy(entryNodeRid);
    if (actorRef === undefined) {
      return;
    }

    try {
      await node.destroyActor(actorRef, 0, destroySignal);
      if (state.actorType !== undefined && state.ownsLocation) {
        await this.options.locationLifecycle?.releaseActor(state.actorType, actor.actorId);
      }
      this.options.actorDestroyedCleanup?.(actor.actorId);
      state.clearAfterDestroy();
      this.states.delete(actor.actorId);
    } catch (error) {
      state.resetDestroying();
      throw error;
    }
  }

  private async createOrGet(
    actorId: string,
    actorType: string,
    failIfExists: boolean,
    request: unknown,
    signal?: AbortSignal,
    claimLocation = true
  ): Promise<{ actor: ZLinkActor; actorRef: ActorRef; created: boolean }> {
    throwIfAborted(signal);
    const state = this.getOrCreateState(actorId);
    const createRequest = this.createRequestMessage(request);
    if (request !== undefined && createRequest.nativeRequest !== undefined) {
      state.setCreateRequestPayload(createRequest.nativeRequest.data());
    }
    const operation = state.getOrStartCreation(
      actorType,
      failIfExists,
      () => this.creation.createActor(actorId, actorType, state, createRequest, claimLocation, signal)
    );

    try {
      const actor = await operation.task;
      return { actor, actorRef: this.actorRefForState(state), created: operation.created };
    } catch (error) {
      state.clearFailedCreation(operation.task);
      if (error instanceof ZLinkFrameworkException) {
        throw error;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor '${actorId}' creation failed: ${error instanceof Error ? error.message : String(error)}`,
        false,
        error
      );
    } finally {
      createRequest.nativeRequest?.close();
    }
  }

  private createRequestMessage(request: unknown): ZLinkActorCreateRequest {
    if (request === undefined) {
      const empty = BindingMessage.from(Buffer.alloc(0));
      return {
        nativeRequest: empty,
        callbackRequest: ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(empty.data()), this.options.messageSerializers)
      };
    }
    const nativeRequest = encodeFrameworkPayloadMessage(request, this.options.messageSerializers);
    return {
      nativeRequest,
      callbackRequest: ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(nativeRequest.data()), this.options.messageSerializers)
    };
  }

  private getOrCreateState(actorId: string): ZLinkActorRuntimeState {
    const existing = this.states.get(actorId);
    if (existing !== undefined) {
      return existing;
    }

    const state = new ZLinkActorRuntimeState(actorId);
    this.states.set(actorId, state);
    return state;
  }

  private actorRefForState(state: ZLinkActorRuntimeState): ActorRef {
    const nativeActorRef = state.nativeActorRef;
    if (nativeActorRef !== undefined) {
      return toFrameworkActorRef(nativeActorRef);
    }
    return {
      nodeRid: this.options.actorCreatedNodeRidProvider?.() ?? BindingRoutingId.from('local') as unknown as RoutingId,
      actorId: state.actorId,
      generation: 0n
    };
  }

  private requireJoinCoordinator(): ZLinkActorJoinCoordinator {
    const coordinator = this.options.joinCoordinator;
    if (coordinator === undefined) {
      throw new ZLinkConfigurationException('Actor join runtime is not started.');
    }
    return coordinator;
  }
}

export class ZLinkActorRuntimeState {
  private creationTask: Promise<ZLinkActor> | undefined;
  private configured = false;
  private context: DefaultZLinkActorContext | undefined;
  private actorTypeValue: string | undefined;
  private actorValue: ZLinkActor | undefined;
  private spotValue: ZLinkSpot | undefined;
  private spotRidValue: RoutingId | undefined;
  private nativeActorRefValue: ZLinkBackendActorRef | undefined;
  private remoteBoundSessionTargetValue: ZLinkRemoteBoundSessionTarget | undefined;
  private remoteActorPacketTargetValue: ZLinkRemoteActorPacketTarget | undefined;
  private createRequestPayloadValue: Buffer | undefined;
  private ownsLocationValue = false;
  private destroying = false;

  constructor(readonly actorId: string) {}

  get actorType(): string | undefined {
    return this.actorTypeValue;
  }

  get actor(): ZLinkActor | undefined {
    return this.actorValue;
  }

  get spot(): ZLinkSpot | undefined {
    return this.spotValue;
  }

  get spotRid(): RoutingId | undefined {
    return this.spotRidValue;
  }

  get nativeActorRef(): ZLinkBackendActorRef | undefined {
    return this.nativeActorRefValue;
  }

  get remoteBoundSessionTarget(): ZLinkRemoteBoundSessionTarget | undefined {
    return this.remoteBoundSessionTargetValue;
  }

  get remoteActorPacketTarget(): ZLinkRemoteActorPacketTarget | undefined {
    return this.remoteActorPacketTargetValue;
  }

  get createRequestPayload(): Buffer | undefined {
    return this.createRequestPayloadValue;
  }

  get isJoined(): boolean {
    return this.spotRidValue !== undefined;
  }

  get ownsLocation(): boolean {
    return this.ownsLocationValue;
  }

  markLocationOwned(): void {
    this.ownsLocationValue = true;
  }

  beginDestroy(entryNodeRid: RoutingId): ZLinkBackendActorRef | undefined {
    if (this.destroying) {
      return undefined;
    }
    const actorRef = this.nativeActorRefValue;
    if (actorRef === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${this.actorId}' does not have a native Actor ref.`
      );
    }
    if (!routingIdsEqual(toFrameworkRoutingId(actorRef.nodeRid), entryNodeRid)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${this.actorId}' is not owned by this Entry Spot.`
      );
    }

    this.destroying = true;
    return actorRef;
  }

  resetDestroying(): void {
    this.destroying = false;
  }

  ensureContext(
    joinCoordinator: ZLinkActorJoinCoordinator | undefined,
    boundSessionFactory: ZLinkActorBoundSessionFactory | undefined,
    messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ): ZLinkActorContext {
    this.context ??= new DefaultZLinkActorContext(
      this,
      joinCoordinator,
      boundSessionFactory,
      messageSerializers
    );
    return this.context;
  }

  getOrStartCreation(
    actorType: string,
    failIfExists: boolean,
    createActor: () => Promise<ZLinkActor>
  ): ZLinkActorCreationOperation {
    if (this.actorTypeValue !== undefined && this.actorTypeValue !== actorType) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorTypeMismatch,
        `Actor '${this.actorId}' already uses actor type '${this.actorTypeValue}', not '${actorType}'.`
      );
    }

    if (this.actorValue !== undefined) {
      if (failIfExists) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorAlreadyExists,
          `Actor '${this.actorId}' already exists.`
        );
      }
      return { task: Promise.resolve(this.actorValue), created: false };
    }

    if (this.creationTask !== undefined) {
      if (failIfExists) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorAlreadyExists,
          `Actor '${this.actorId}' is already being created.`
        );
      }
      return { task: this.creationTask, created: false };
    }

    this.actorTypeValue = actorType;
    this.creationTask = createActor();
    return { task: this.creationTask, created: true };
  }

  clearFailedCreation(task: Promise<ZLinkActor>): void {
    if (this.creationTask === task && this.actorValue === undefined) {
      this.creationTask = undefined;
      this.actorTypeValue = undefined;
      this.createRequestPayloadValue = undefined;
      this.configured = false;
    }
  }

  bindActor(actor: ZLinkActor, context: ZLinkActorContext): void {
    if (actor.actorId !== this.actorId) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor state id '${this.actorId}' does not match actor id '${actor.actorId}'.`
      );
    }
    if (actor.context !== context) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor '${this.actorId}' must expose the context provided by its factory.`
      );
    }

    this.actorValue = actor;
    this.creationTask = undefined;
    if (!this.configured) {
      actor.configure?.();
      this.configured = true;
    }
  }

  ensureNativeActorRef(node: ZLinkBackendSpotNode, request?: Message): ZLinkBackendActorRef {
    this.nativeActorRefValue ??= node.actorLookup(this.actorId) ?? node.createActor(this.actorId, request);
    return this.nativeActorRefValue;
  }

  setNativeActorRef(actorRef: ZLinkBackendActorRef): void {
    this.nativeActorRefValue = actorRef;
  }

  setRemoteBoundSessionTarget(target: ZLinkRemoteBoundSessionTarget | undefined): void {
    this.remoteBoundSessionTargetValue = target;
  }

  setRemoteActorPacketTarget(target: ZLinkRemoteActorPacketTarget | undefined): void {
    this.remoteActorPacketTargetValue = target;
  }

  setCreateRequestPayload(payload: Buffer | Uint8Array): void {
    this.createRequestPayloadValue = Buffer.from(payload);
  }

  setJoinedSpot(spotRid: RoutingId, spot?: ZLinkSpot): void {
    this.spotRidValue = spotRid;
    this.spotValue = spot;
  }

  clearJoinedSpot(): void {
    this.spotRidValue = undefined;
    this.spotValue = undefined;
  }

  clearAfterDestroy(): void {
    this.creationTask = undefined;
    this.configured = false;
    this.context = undefined;
    this.actorTypeValue = undefined;
    this.actorValue = undefined;
    this.spotValue = undefined;
    this.spotRidValue = undefined;
    this.nativeActorRefValue = undefined;
    this.remoteBoundSessionTargetValue = undefined;
    this.remoteActorPacketTargetValue = undefined;
    this.createRequestPayloadValue = undefined;
    this.ownsLocationValue = false;
    this.destroying = false;
  }
}

export class ZLinkActorNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  constructor(private readonly options: ZLinkActorNativeJoinCoordinatorOptions) {}

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    throwIfAborted(signal);
    const actorRef = state.ensureNativeActorRef(this.options.node);
    const remoteAddress = this.options.remoteAddressResolver === undefined
      ? undefined
      : await this.options.remoteAddressResolver.resolve(spotRid, signal);
    const isRemoteJoin = remoteAddress !== undefined && String(remoteAddress.targetNodeRid) !== String(actorRef.nodeRid);
    if (isRemoteJoin && this.canUseRoutedTransport(remoteAddress)) {
      return await this.joinRemoteSpot(actor, state, actorRef, remoteAddress, request, timeoutMs, signal);
    }
    const joinRequest = isRemoteJoin
      ? this.encodeRemoteNativeJoinRequest(actor, state, request)
      : request;
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      if (signal?.aborted === true) {
        reject(new Error('The operation was aborted.'));
        return;
      }
      const submitted = this.options.node.joinActor(
          actorRef,
          remoteAddress?.targetNodeRid ?? actorRef.nodeRid,
          toBackendRoutingId(spotRid),
          joinRequest,
        (joinResult: ZLinkBackendActorJoinResult, replyParts: readonly Message[]) => {
          resolve({ result: joinResult, parts: replyParts });
        },
        timeoutMs
      );
      if (!submitted) {
        reject(new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor join submit failed for '${actor.actorId}'.`
        ));
      }
    });

    if (result.result !== 0) {
      this.disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    state.setNativeActorRef(result.actor);
    state.setJoinedSpot(toFrameworkRoutingId(result.joinedSpotRid));
    state.setRemoteActorPacketTarget(undefined);
    if (state.actorType !== undefined) {
      await this.options.locationLifecycle?.notifyActorJoinedSpot(
        state.actorType,
        actor.actorId,
        remoteAddress?.routerChannelId ?? '',
        toFrameworkRoutingId(result.joinedSpotRid)
      );
    }
    if (result.joinResultCode === 0) {
      await this.options.remoteActorBinder?.(toFrameworkActorRef(result.actor), signal, !isRemoteJoin);
    }
    try {
      return {
        resultCode: result.joinResultCode,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      this.disposeParts(parts.slice(1));
      if (joinRequest !== request) {
        joinRequest.close();
      }
    }
  }

  private encodeRemoteNativeJoinRequest(actor: ZLinkActor, state: ZLinkActorRuntimeState, request: Message): Message {
    const actorType = state.actorType;
    const actorRef = state.nativeActorRef;
    if (actorType === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' does not have an actor type for remote SPOT join.`
      );
    }
      return BindingMessage.from(Buffer.from(JSON.stringify({
        packetName: REMOTE_ACTOR_JOIN_PACKET,
        actorType,
        actorNodeRid: actorRef === undefined ? undefined : String(actorRef.nodeRid),
        actorNodeRidHex: actorRef === undefined ? undefined : encodeRoutingIdHex(actorRef.nodeRid),
        actorGeneration: actorRef === undefined ? undefined : actorRef.generation.toString(),
        actorCreateRequest: state.createRequestPayload?.toString('base64'),
        request: request.data().toString('base64')
      })));
  }

  private async joinRemoteSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const actorType = state.actorType;
    if (actorType === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actor.actorId}' does not have an actor type for remote SPOT join.`
      );
    }
    if (this.canUseRoutedTransport(remoteAddress)) {
      const routedTransport = this.options.routedTransport;
      if (routedTransport === undefined) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actor.actorId}' remote route transport is not configured.`
        );
      }
      const entrySpotRid = this.options.node.entrySpot().routingId;
      const boundSessionTarget = state.remoteBoundSessionTarget;
      const sourceSpotRid = String(boundSessionTarget?.spotRid ?? entrySpotRid);
      const sourceSpotRidHex = encodeRoutingIdHex(boundSessionTarget?.spotRid ?? entrySpotRid);
      const joinRequest = {
        packetName: REMOTE_ACTOR_JOIN_PACKET,
        spotRid: String(remoteAddress.spotRid),
        actorId: actor.actorId,
        actorType,
        actorNodeRid: String(actorRef.nodeRid),
        actorNodeRidHex: encodeRoutingIdHex(actorRef.nodeRid),
        actorGeneration: actorRef.generation.toString(),
        actorCreateRequest: state.createRequestPayload?.toString('base64'),
        sourceSpotRid,
        sourceSpotRidHex,
        routerChannelId: remoteAddress.routerChannelId,
        boundSessionRouterChannelId: boundSessionTarget?.routerChannelId,
        boundSessionTargetNodeRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.targetNodeRid),
        boundSessionTargetNodeRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.targetNodeRid),
        boundSessionSpotRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.spotRid),
        boundSessionSpotRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.spotRid),
        request: request.data().toString('base64')
      };
      const reply = typeof routedTransport.requestToSpot === 'function'
        ? await routedTransport.requestToSpot<ZLinkRemoteActorJoinReply & { readonly reply?: string }>(
            remoteAddress,
            joinRequest,
            {
              packetName: REMOTE_ACTOR_JOIN_PACKET,
              timeoutMs,
              signal
            }
          )
        : await routedTransport.request<ZLinkRemoteActorJoinReply & { readonly reply?: string }>(
            remoteAddress.routerChannelId,
            remoteAddress.targetNodeRid,
            REMOTE_ACTOR_JOIN_PACKET,
            joinRequest,
            timeoutMs,
            signal
          );
      return await this.applyRemoteJoinResult(
        state,
        reply,
        remoteAddress,
        reply.reply == null ? undefined : BindingMessage.from(Buffer.from(reply.reply, 'base64')),
        signal
      );
    }
    const entrySpotRid = this.options.node.entrySpot().routingId;
    const boundSessionTarget = state.remoteBoundSessionTarget;
    const sourceSpotRid = boundSessionTarget?.spotRid ?? entrySpotRid;
    const joinPayload = {
      packetName: REMOTE_ACTOR_JOIN_PACKET,
      actorId: actor.actorId,
      actorType,
      actorNodeRid: String(actorRef.nodeRid),
      actorNodeRidHex: encodeRoutingIdHex(actorRef.nodeRid),
      actorGeneration: actorRef.generation.toString(),
      actorCreateRequest: state.createRequestPayload?.toString('base64'),
      routerChannelId: remoteAddress.routerChannelId,
      sourceSpotRid: String(sourceSpotRid),
      sourceSpotRidHex: encodeRoutingIdHex(sourceSpotRid),
      boundSessionRouterChannelId: boundSessionTarget?.routerChannelId,
      boundSessionTargetNodeRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.targetNodeRid),
      boundSessionTargetNodeRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.targetNodeRid),
      boundSessionSpotRid: boundSessionTarget === undefined ? undefined : String(boundSessionTarget.spotRid),
      boundSessionSpotRidHex: boundSessionTarget === undefined ? undefined : encodeRoutingIdHex(boundSessionTarget.spotRid),
      request: request.data().toString('base64')
    } satisfies ZLinkRemoteActorJoinRequest & { readonly request: string };
    const transport = this.options.routedTransport;
    if (transport?.requestRawToSpot !== undefined) {
      const payload = BindingMessage.from(Buffer.from(JSON.stringify(joinPayload)));
      try {
        const parts = await transport.requestRawToSpot(remoteAddress, payload, { timeoutMs, signal });
        try {
          if (parts.length === 0) {
            throw new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.ActorRouteNotFound,
              `Remote actor join reply was empty for '${actor.actorId}'.`
            );
          }
          const reply = JSON.parse(parts[0].getString('utf8')) as ZLinkRemoteActorJoinReply;
          return await this.applyRemoteJoinResult(state, reply, remoteAddress, parts[1], signal);
        } finally {
          parts[0]?.close();
          this.disposeParts(parts.slice(2));
        }
      } finally {
        payload.close();
      }
    }
    const {
      request: _encodedRequest,
      ...headerPayload
    } = joinPayload;
    const header = BindingMessage.from(Buffer.from(JSON.stringify({
      ...headerPayload
    } satisfies ZLinkRemoteActorJoinRequest)));
    const outbound = this.options.node.getOrCreateSpot(`__zlink.actor.join.${String(actorRef.nodeRid)}`).spot;
    try {
      const parts = await new Promise<readonly Message[]>((resolve, reject) => {
        if (signal?.aborted === true) {
          reject(new Error('The operation was aborted.'));
          return;
        }
        const submitted = outbound.requestToSpot(
          remoteAddress.targetNodeRid,
          remoteAddress.spotRid,
          [header, request],
          (result, replyParts) => {
            if (result !== 0) {
              reject(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join failed for '${actor.actorId}' to SPOT '${remoteAddress.spotRid}' with result ${result}.`
              ));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join submit failed for '${actor.actorId}'.`
          ));
        }
      });
      try {
        if (parts.length === 0) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Remote actor join reply was empty for '${actor.actorId}'.`
          );
        }
        const reply = JSON.parse(parts[0].getString('utf8')) as ZLinkRemoteActorJoinReply;
        return await this.applyRemoteJoinResult(state, reply, remoteAddress, parts[1], signal);
      } finally {
        parts[0]?.close();
        this.disposeParts(parts.slice(2));
      }
    } finally {
      header.close();
    }
  }

  /**
   * Decode a remote-actor-join reply into a framework ActorRef, apply the
   * accepted-join side effects to runtime state (native ref, joined spot,
   * remote packet target, remote binder), and build the join result. Shared by
   * all three remote-join transports (routed request, raw-to-spot, fallback
   * spot) so the accept/decode semantics stay identical across them.
   */
  private async applyRemoteJoinResult(
    state: ZLinkActorRuntimeState,
    reply: ZLinkRemoteActorJoinReply,
    remoteAddress: ZLinkSpotRemoteAddress,
    replyMessage: Message | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const resultActor = {
      nodeRid: decodeWireRoutingId(reply.actorNodeRid, reply.actorNodeRidHex),
      actorId: reply.actorId,
      generation: BigInt(reply.actorGeneration)
    } as ActorRef;
    if (reply.accepted) {
      state.setNativeActorRef(resultActor as unknown as ZLinkBackendActorRef);
      state.setJoinedSpot(remoteAddress.spotRid);
      state.setRemoteActorPacketTarget({
        routerChannelId: remoteAddress.routerChannelId,
        targetNodeRid: remoteAddress.targetNodeRid,
        spotRid: remoteAddress.spotRid,
        spotKind: remoteAddress.spotKind
      });
      if (state.actorType !== undefined) {
        await this.options.locationLifecycle?.notifyActorJoinedSpot(
          state.actorType,
          reply.actorId,
          remoteAddress.routerChannelId,
          remoteAddress.spotRid
        );
      }
      await this.options.remoteActorBinder?.(resultActor, signal, true);
    }
    return {
      resultCode: reply.accepted ? 0 : 1,
      actor: resultActor,
      reply: replyMessage
    };
  }

  private canUseRoutedTransport(remoteAddress: ZLinkSpotRemoteAddress): boolean {
    const transport = this.options.routedTransport;
    if (transport === undefined) {
      return false;
    }
    if (transport.canRoutePacketChannel !== undefined) {
      return transport.canRoutePacketChannel(remoteAddress.routerChannelId);
    }
    return transport.canRouteChannel?.(remoteAddress.routerChannelId) !== false;
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    throwIfAborted(signal);
    const actorRef = state.ensureNativeActorRef(this.options.node);
    const remoteEntry = String(nodeRid) === String(actorRef.nodeRid)
      ? undefined
      : await this.tryResolveRemoteEntry(nodeRid, signal);
    const isRemoteJoin = remoteEntry !== undefined && String(remoteEntry.targetNodeRid) !== String(actorRef.nodeRid);
    if (isRemoteJoin && this.canUseRoutedTransport(remoteEntry)) {
      const result = await this.joinRemoteSpot(
        actor,
        state,
        actorRef,
        {
          routerChannelId: remoteEntry.routerChannelId,
          targetNodeRid: remoteEntry.targetNodeRid,
          spotRid: remoteEntry.spotRid,
          spotKind: ZLinkSpotKind.Entry
        },
        request,
        timeoutMs,
        signal
      );
      if (result.resultCode === 0) {
        state.clearJoinedSpot();
        state.setRemoteActorPacketTarget(undefined);
        if (state.actorType !== undefined) {
          await this.options.locationLifecycle?.notifyActorLeftSpot(state.actorType, actor.actorId);
        }
      }
      return result;
    }
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinEntrySpotResult;
      parts: readonly Message[];
    }>(
      (resolve, reject) => {
        const submitted = this.options.node.joinActorEntrySpot(
          actorRef,
          toBackendRoutingId(nodeRid),
          request,
          (entryResult, replyParts) => resolve({ result: entryResult, parts: replyParts }),
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            `Actor entry SPOT join submit failed for '${actor.actorId}'.`
          ));
        }
      }
    );

    if (result.result !== 0) {
      this.disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor entry SPOT join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    if (result.joinResultCode === 0) {
      state.setNativeActorRef(result.actor);
      state.clearJoinedSpot();
      if (state.actorType !== undefined) {
        await this.options.locationLifecycle?.notifyActorLeftSpot(state.actorType, actor.actorId);
      }
    }
    try {
      return {
        resultCode: result.joinResultCode,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      this.disposeParts(parts.slice(1));
    }
  }

  private disposeParts(parts: readonly Message[]): void {
    for (const part of parts) {
      part.close();
    }
  }

  private async tryResolveRemoteEntry(
    nodeRid: RoutingId,
    signal: AbortSignal | undefined
  ): Promise<ZLinkSpotRemoteAddress | undefined> {
    if (this.options.remoteAddressResolver === undefined) {
      return undefined;
    }
    try {
      return await this.options.remoteAddressResolver.resolve(nodeRid, signal);
    } catch {
      return undefined;
    }
  }
}

export class DefaultZLinkActorContext implements ZLinkActorContext {
  readonly boundSession: ZLinkBoundSession;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly joinCoordinator: ZLinkActorJoinCoordinator | undefined,
    boundSessionFactory: ZLinkActorBoundSessionFactory | undefined,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
    this.boundSession = boundSessionFactory?.(state.actorId) ?? new UnboundZLinkSession();
  }

  get spotRid(): RoutingId | undefined {
    return this.state.spotRid;
  }

  get isJoined(): boolean {
    return this.state.isJoined;
  }

  get actorRef(): ActorRef | undefined {
    const actorRef = this.state.nativeActorRef;
    return actorRef === undefined
      ? undefined
      : toFrameworkActorRef(actorRef);
  }

  getSpot<TSpot extends ZLinkSpot>(spotType?: Type<TSpot>): ZLinkSpot | TSpot {
    const spot = this.state.spot;
    if (spot === undefined) {
      throw new ZLinkConfigurationException('Actor has not joined a SPOT.');
    }
    if (spotType !== undefined && !(spot instanceof spotType)) {
      throw new ZLinkConfigurationException('Actor joined SPOT has a different spot type.');
    }
    return spot;
  }

  joinSpot(spotRid: RoutingId, request?: unknown): ZLinkActorJoinSpotCall {
    return new DefaultZLinkActorJoinSpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      spotRid,
      request,
      this.messageSerializers
    );
  }

  joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall {
    return new DefaultZLinkActorJoinEntrySpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      nodeRid,
      request,
      this.messageSerializers
    );
  }

  private requireActor(): ZLinkActor {
    if (this.state.actor === undefined) {
      throw new ZLinkConfigurationException('Actor context is not bound to an actor.');
    }
    return this.state.actor;
  }

  private requireJoinCoordinator(): ZLinkActorJoinCoordinator {
    if (this.joinCoordinator === undefined) {
      throw new ZLinkConfigurationException('Actor join runtime is not started.');
    }
    return this.joinCoordinator;
  }
}

export class ZLinkActorDispatchMailbox {
  private tail: Promise<unknown> = Promise.resolve();

  submit<T>(operation: () => Promise<T> | T): Promise<T> {
    const next = this.tail.then(operation, operation);
    this.tail = next.catch(() => undefined);
    return next;
  }
}

export class ZLinkActorDispatchMailboxSet {
  private readonly mailboxes = new Map<string, ZLinkActorDispatchMailbox>();

  submit<T>(actorId: string, operation: () => Promise<T> | T): Promise<T> {
    let mailbox = this.mailboxes.get(actorId);
    if (mailbox === undefined) {
      mailbox = new ZLinkActorDispatchMailbox();
      this.mailboxes.set(actorId, mailbox);
    }
    return mailbox.submit(operation);
  }
}

export interface ZLinkActorDispatchSnapshot {
  readonly actor: ZLinkActor;
  readonly actorId: string;
  readonly actorType?: string;
  readonly spotRid?: RoutingId;
  readonly spot?: ZLinkSpot;
  readonly isJoined: boolean;
}

export interface ZLinkActorDispatchRouterOptions {
  readonly entryExecutor?: {
    execute<T>(operation: () => Promise<T> | T): Promise<T>;
  };
}

export class ZLinkActorDispatchRouter {
  private readonly mailboxes = new ZLinkActorDispatchMailboxSet();

  constructor(
    private readonly manager: Pick<DefaultZLinkActorManager, 'getState'>,
    private readonly options: ZLinkActorDispatchRouterOptions = {}
  ) {}

  submit<T>(
    actorId: string,
    operation: (snapshot: ZLinkActorDispatchSnapshot) => Promise<T> | T
  ): Promise<T> {
    return this.mailboxes.submit(actorId, () => {
      const snapshot = this.createSnapshot(actorId);
      return operation(snapshot);
    });
  }

  private createSnapshot(actorId: string): ZLinkActorDispatchSnapshot {
    const state = this.manager.getState(actorId);
    if (state?.actor === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor '${actorId}' is not created.`
      );
    }

    return {
      actor: state.actor,
      actorId: state.actorId,
      actorType: state.actorType,
      spotRid: state.spotRid,
      spot: state.spot,
      isJoined: state.isJoined
    };
  }
}

export class ZLinkSpotActorHandlerRegistryRuntime {
  private readonly packets = new Map<string, ZLinkActorPacketDescriptor>();

  addPacket(descriptor: ZLinkActorPacketDescriptor): this {
    const key = packetKey(descriptor.kind, descriptor.actorType, descriptor.packetName);
    if (this.packets.has(key)) {
      throw new ZLinkConfigurationException(
        `Actor packet '${descriptor.packetName}' for '${descriptor.actorType.name}' is already registered.`
      );
    }
    this.packets.set(key, descriptor);
    return this;
  }

  resolvePacket(
    kind: ZLinkActorPacketKind,
    actor: ZLinkActor,
    packetName: string
  ): ZLinkActorPacketDescriptor | undefined {
    const actorType = actor.constructor as Type<ZLinkActor>;
    const exact = this.packets.get(packetKey(kind, actorType, packetName));
    if (exact !== undefined) {
      return exact;
    }
    const wildcard = this.packets.get(packetKey(kind, Object as unknown as Type<ZLinkActor>, packetName));
    if (wildcard !== undefined) {
      return wildcard;
    }

    for (const descriptor of this.packets.values()) {
      if (
        descriptor.kind === kind
        && descriptor.packetName === packetName
        && actor instanceof descriptor.actorType
      ) {
        return descriptor;
      }
    }
    return undefined;
  }

}

export interface ZLinkSpotActorDispatcherOptions {
  readonly registry: ZLinkSpotActorHandlerRegistryRuntime;
  readonly spot: ZLinkSpot;
  readonly handlerFactory?: (handlerType: Type) => unknown;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly serial?: { execute<T>(operation: () => Promise<T> | T): Promise<T> };
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
}

export class DefaultZLinkSpotActorReplyOptions implements ZLinkSpotActorReplyOptions {
  private readonly selectedMetadata = new Map<string, string>();
  private compressionEnabled = false;

  metadata(key: string, value: string): this {
    this.selectedMetadata.set(key, value);
    return this;
  }

  compress(enabled = true): this {
    this.compressionEnabled = enabled;
    return this;
  }

  snapshot(): ZLinkSpotActorReplyOptionsSnapshot {
    return {
      metadata: new Map(this.selectedMetadata),
      compressPayload: this.compressionEnabled
    };
  }
}

export interface ZLinkSpotActorReplyOptionsSnapshot {
  readonly metadata: ReadonlyMap<string, string>;
  readonly compressPayload: boolean;
}

export class ZLinkSpotActorDispatcher {
  constructor(private readonly options: ZLinkSpotActorDispatcherOptions) {}

  dispatchSend<TMessage>(
    actor: ZLinkActor,
    packetName: string,
    message: TMessage,
    context: Partial<ZLinkSpotActorSendContext> = {}
  ): Promise<void> {
    return this.execute(async () => {
      const descriptor = this.requirePacket(ZLinkActorPacketKind.Send, actor, packetName);
      const handler = this.createHandler<ZLinkSpotActorSendHandler<ZLinkSpot, ZLinkActor, TMessage>>(descriptor);
      await handler.handle(this.options.spot, actor, this.createSendContext(packetName, context), message);
    });
  }

  dispatchRequest<TRequest, TReply>(
    actor: ZLinkActor,
    packetName: string,
    request: TRequest,
    context: Partial<ZLinkSpotActorRequestContext> = {}
  ): Promise<TReply> {
    return this.dispatchRequestThen<TRequest, TReply, TReply>(actor, packetName, request, context, (reply) => reply);
  }

  dispatchRequestThen<TRequest, TReply, TResult>(
    actor: ZLinkActor,
    packetName: string,
    request: TRequest,
    context: Partial<ZLinkSpotActorRequestContext> = {},
    afterReply: (reply: TReply) => Promise<TResult> | TResult
  ): Promise<TResult> {
    return this.execute(async () => {
      const descriptor = this.requirePacket(ZLinkActorPacketKind.Request, actor, packetName);
      const handler = this.createHandler<ZLinkSpotActorRequestHandler<ZLinkSpot, ZLinkActor, TRequest, TReply>>(descriptor);
      const reply = await handler.handle(this.options.spot, actor, this.createRequestContext(packetName, context), request);
      return await afterReply(reply);
    });
  }

  admitActorJoin(
    actor: ZLinkActor,
    request: Message,
    commit: () => Promise<void> | void
  ): Promise<ZLinkSpotActorJoinResponse> {
    return this.execute(async () => {
      const payload = wrapFrameworkPayloadMessage(request, this.options.messageSerializers);
      const result = await this.options.spot.onActorJoin?.(actor, payload) ?? { accepted: false };
      if (!result.accepted) {
        return result;
      }
      await commit();
      await this.options.spot.onJoinedActor?.(actor);
      return result;
    });
  }

  notifyJoinActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onJoinedActor?.(actor));
  }

  notifyLeaveActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onLeaveActor?.(actor));
  }

  notifyDisconnectActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onDisconnectActor?.(actor));
  }

  private requirePacket(
    kind: ZLinkActorPacketKind,
    actor: ZLinkActor,
    packetName: string
  ): ZLinkActorPacketDescriptor {
    const descriptor = this.options.registry.resolvePacket(kind, actor, packetName);
    if (descriptor === undefined) {
      throw actorDispatchHandlerNotFound(`No Spot actor ${kind} handler is registered for '${packetName}'.`);
    }
    this.ensureActorType(descriptor, actor);
    return descriptor;
  }

  private ensureActorType(descriptor: ZLinkActorPacketDescriptor, actor: ZLinkActor): void {
    if (descriptor.actorType === (Object as unknown as Type<ZLinkActor>)) {
      return;
    }
    if (actor instanceof descriptor.actorType) {
      return;
    }
    throw actorDispatchHandlerNotFound(
      `Actor handler '${descriptor.handlerType.name}' expects actor '${descriptor.actorType.name}'.`
    );
  }

  private createHandler<THandler>(descriptor: Pick<ZLinkActorPacketDescriptor, 'handlerType'>): THandler {
    const handler = this.options.handlerFactory?.(descriptor.handlerType)
      ?? this.options.providerResolver?.get?.(descriptor.handlerType)
      ?? new descriptor.handlerType();
    return handler as THandler;
  }

  private execute<T>(operation: () => Promise<T> | T): Promise<T> {
    return this.options.serial?.execute(operation) ?? Promise.resolve().then(operation);
  }

  private createSendContext(
    packetName: string,
    context: Partial<ZLinkSpotActorSendContext>
  ): ZLinkSpotActorSendContext {
    return {
      ...context,
      packetName,
      metadata: context.metadata ?? {}
    } as ZLinkSpotActorSendContext;
  }

  private createRequestContext(
    packetName: string,
    context: Partial<ZLinkSpotActorRequestContext>
  ): ZLinkSpotActorRequestContext {
    return {
      ...this.createSendContext(packetName, context),
      reply: context.reply ?? new DefaultZLinkSpotActorReplyOptions()
    } as ZLinkSpotActorRequestContext;
  }
}

class DefaultZLinkActorJoinSpotCall implements ZLinkActorJoinSpotCall {
  private timeoutMs: number | undefined;
  private readonly yieldTurn: ZLinkSpotSerialTurn | undefined;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly spotRid: RoutingId,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
    this.yieldTurn = captureZLinkSpotSerialTurn();
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const requestMessage = this.request === undefined
      ? BindingMessage.from(Buffer.alloc(0))
      : encodeFrameworkPayloadMessage(this.request, this.messageSerializers);
    const ownsRequest = ownsFrameworkPayloadMessage(this.request);
    try {
      const result = await this.coordinator.joinSpot(
        this.actor,
        this.state,
        this.spotRid,
        requestMessage,
        this.timeoutMs,
        signal
      );
      return {
        ...result,
        reply: result.reply === undefined
          ? undefined
          : decodeFrameworkPayloadMessage<TReply>(result.reply, this.messageSerializers)
      };
    } finally {
      if (ownsRequest) {
        requestMessage.close();
      }
    }
  }

  yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    if (this.yieldTurn === undefined) {
      return Promise.reject(new ZLinkConfigurationException(
        'yield requires a framework Spot handler turn captured when the call object was created.'
      ));
    }
    return this.yieldTurn.yieldPromise(this.submit<TReply>(signal));
  }
}

class DefaultZLinkActorJoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
  private timeoutMs: number | undefined;
  private readonly yieldTurn: ZLinkSpotSerialTurn | undefined;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly nodeRid: RoutingId,
    private readonly request: unknown,
    private readonly messageSerializers: ReadonlyMap<string, ZLinkMessageSerializer> | undefined
  ) {
    this.yieldTurn = captureZLinkSpotSerialTurn();
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    const requestMessage = encodeFrameworkPayloadMessage(this.request, this.messageSerializers);
    const ownsRequest = ownsFrameworkPayloadMessage(this.request);
    try {
      const result = await this.coordinator.joinEntrySpot(
        this.actor,
        this.state,
        this.nodeRid,
        requestMessage,
        this.timeoutMs,
        signal
      );
      return {
        ...result,
        reply: result.reply === undefined
          ? undefined
          : decodeFrameworkPayloadMessage<TReply>(result.reply, this.messageSerializers)
      };
    } finally {
      if (ownsRequest) {
        requestMessage.close();
      }
    }
  }

  yield<TReply = unknown>(signal?: AbortSignal): Promise<ZLinkActorJoinResult<TReply>> {
    if (this.yieldTurn === undefined) {
      return Promise.reject(new ZLinkConfigurationException(
        'yield requires a framework Spot handler turn captured when the call object was created.'
      ));
    }
    return this.yieldTurn.yieldPromise(this.submit<TReply>(signal));
  }
}

class UnboundZLinkSession implements ZLinkBoundSession {
  send(): never {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      'Actor session is not bound.',
      true
    );
  }

  async disconnect(): Promise<void> {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      'Actor session is not bound.',
      true
    );
  }
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}

function normalizeCreateRequestArgs(
  signalOrRequest: AbortSignal | unknown,
  signal: AbortSignal | undefined
): { readonly request: unknown; readonly signal: AbortSignal | undefined } {
  if (isAbortSignal(signalOrRequest)) {
    return { request: undefined, signal: signalOrRequest };
  }
  return { request: signalOrRequest, signal };
}

function isAbortSignal(value: unknown): value is AbortSignal {
  return typeof value === 'object'
    && value !== null
    && typeof (value as { aborted?: unknown }).aborted === 'boolean'
    && typeof (value as { addEventListener?: unknown }).addEventListener === 'function';
}

function toBackendRoutingId(routingId: RoutingId): ZLinkBackendActorRef['nodeRid'] {
  return routingId as unknown as ZLinkBackendActorRef['nodeRid'];
}

function toFrameworkRoutingId(routingId: ZLinkBackendActorRef['nodeRid']): RoutingId {
  const value = routingId as unknown;
  return value instanceof BindingRoutingId
    ? value as unknown as RoutingId
    : BindingRoutingId.from(String(routingId)) as unknown as RoutingId;
}

function encodeRoutingIdHex(routingId: RoutingId): string | undefined {
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  return typeof toHex === 'function' ? toHex.call(routingId) : undefined;
}

function serializeActorRef(nodeRid: RoutingId, actorId: string, generation: bigint): string {
  return `${encodeRoutingIdHex(nodeRid) ?? String(nodeRid)}:${generation}:${actorId}`;
}

function routingIdsEqual(left: RoutingId, right: RoutingId): boolean {
  const leftHex = encodeRoutingIdHex(left);
  const rightHex = encodeRoutingIdHex(right);
  if (leftHex !== undefined || rightHex !== undefined) {
    return leftHex === rightHex;
  }
  return String(left) === String(right);
}

function decodeWireRoutingId(text: string, hex: string | undefined): RoutingId {
  return hex === undefined
    ? BindingRoutingId.from(text) as unknown as RoutingId
    : BindingRoutingId.fromHex(hex) as unknown as RoutingId;
}

function toFrameworkActorRef(actor: ZLinkBackendActorRef): ActorRef {
  return {
    nodeRid: toFrameworkRoutingId(actor.nodeRid),
    actorId: actor.actorId,
    generation: actor.generation
  };
}

function packetKey(kind: ZLinkActorPacketKind, actorType: Type<ZLinkActor>, packetName: string): string {
  return `${kind}:${actorType.name}:${packetName}`;
}

function isMessage(value: unknown): value is Message {
  if (value instanceof ZLinkMessage) {
    return false;
  }
  return typeof value === 'object'
    && value !== null
    && typeof (value as { data?: unknown }).data === 'function';
}

function ownsFrameworkPayloadMessage(value: unknown): boolean {
  return value === undefined || !(isMessage(value) || (isZLinkMessage(value) && value.isEncoded()));
}

function actorDispatchHandlerNotFound(message: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
    message
  );
}

async function createProviderInstance<T>(
  type: Type<T>,
  resolver: ZLinkProviderResolver | undefined
): Promise<T> {
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  return new (type as new () => T)();
}
