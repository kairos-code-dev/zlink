import type {
  ActorRef,
  Message,
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
  ZLinkSpotActorSendHandler
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { Message as BindingMessage } from '@zlink-systems/zlink';
import { ZLinkConfigurationException } from '../configuration';
import type {
  ZLinkBackendActorJoinEntrySpotResult,
  ZLinkBackendActorJoinResult,
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';

export interface ZLinkActorManagerOptions {
  readonly actorFactories: ReadonlyMap<string, Type | ZLinkActorFactory>;
  readonly joinCoordinator?: ZLinkActorJoinCoordinator;
  readonly nativeActorNode?: ZLinkBackendSpotNode;
  readonly actorCreatedNodeRidProvider?: () => RoutingId | undefined;
  readonly actorCreatedNotifier?: (
    nodeRid: RoutingId,
    actor: ZLinkActor,
    signal?: AbortSignal
  ) => Promise<void>;
  readonly actorDestroyedCleanup?: (actorId: string) => void;
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
  ): Promise<ZLinkActorJoinResult>;
  joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ActorRef>;
}

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendSpotNode;
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

export class DefaultZLinkActorManager implements ZLinkActorManager {
  private readonly states = new Map<string, ZLinkActorRuntimeState>();

  constructor(private readonly options: ZLinkActorManagerOptions) {}

  async create(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor> {
    const result = await this.createOrGet(actorId, actorType, true, signal);
    return result.actor;
  }

  async find(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined> {
    throwIfAborted(signal);
    return this.states.get(actorId)?.actor;
  }

  async getOrCreate(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor> {
    const result = await this.createOrGet(actorId, actorType, false, signal);
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
    signal?: AbortSignal
  ): Promise<{ actor: ZLinkActor; created: boolean }> {
    throwIfAborted(signal);
    const state = this.getOrCreateState(actorId);
    const operation = state.getOrStartCreation(
      actorType,
      failIfExists,
      () => this.createActorCore(actorId, actorType, state, signal)
    );

    try {
      const actor = await operation.task;
      return { actor, created: operation.created };
    } catch (error) {
      state.clearFailedCreation(operation.task);
      if (error instanceof ZLinkFrameworkException) {
        throw error;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorCreateFailed,
        `Actor '${actorId}' creation failed.`,
        false,
        error
      );
    }
  }

  private async createActorCore(
    actorId: string,
    actorType: string,
    state: ZLinkActorRuntimeState,
    signal?: AbortSignal
  ): Promise<ZLinkActor> {
    const factory = await this.createFactory(actorType);
    const context = state.ensureContext(
      this.options.joinCoordinator,
      this.options.boundSessionFactory
    );
    const actor = await factory.create(actorId, context, signal);
    try {
      state.bindActor(actor, context);
      if (this.options.nativeActorNode !== undefined) {
        const actorRef = state.ensureNativeActorRef(this.options.nativeActorNode);
        await this.options.actorCreatedNotifier?.(
          toFrameworkRoutingId(actorRef.nodeRid),
          actor,
          signal
        );
      } else {
        const nodeRid = this.options.actorCreatedNodeRidProvider?.();
        if (nodeRid !== undefined) {
          await this.options.actorCreatedNotifier?.(nodeRid, actor, signal);
        }
      }
    } catch (error) {
      state.clearAfterDestroy();
      throw error;
    }
    return actor;
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

  private getOrCreateState(actorId: string): ZLinkActorRuntimeState {
    const existing = this.states.get(actorId);
    if (existing !== undefined) {
      return existing;
    }

    const state = new ZLinkActorRuntimeState(actorId);
    this.states.set(actorId, state);
    return state;
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

  get isJoined(): boolean {
    return this.spotRidValue !== undefined;
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
    if (toFrameworkRoutingId(actorRef.nodeRid) !== entryNodeRid) {
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
    boundSessionFactory: ZLinkActorBoundSessionFactory | undefined
  ): ZLinkActorContext {
    if (this.context === undefined) {
      this.context = new DefaultZLinkActorContext(this, joinCoordinator, boundSessionFactory);
    }
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

  ensureNativeActorRef(node: ZLinkBackendSpotNode): ZLinkBackendActorRef {
    if (this.nativeActorRefValue === undefined) {
      this.nativeActorRefValue = node.actorLookup(this.actorId) ?? node.createActor(this.actorId);
    }
    return this.nativeActorRefValue;
  }

  setNativeActorRef(actorRef: ZLinkBackendActorRef): void {
    this.nativeActorRefValue = actorRef;
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
  ): Promise<ZLinkActorJoinResult> {
    throwIfAborted(signal);
    const actorRef = state.ensureNativeActorRef(this.options.node);
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      if (signal?.aborted) {
        reject(new Error('The operation was aborted.'));
        return;
      }
      const submitted = this.options.node.joinActor(
        actorRef,
        actorRef.nodeRid,
        toBackendRoutingId(spotRid),
        request,
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

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ActorRef> {
    throwIfAborted(signal);
    const actorRef = state.ensureNativeActorRef(this.options.node);
    const result = await new Promise<ZLinkBackendActorJoinEntrySpotResult>(
      (resolve, reject) => {
        const submitted = this.options.node.joinActorEntrySpot(
          actorRef,
          toBackendRoutingId(nodeRid),
          (entryResult) => resolve(entryResult),
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
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor entry SPOT join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    state.setNativeActorRef(result.actor);
    state.clearJoinedSpot();
    return toFrameworkActorRef(result.actor);
  }

  private disposeParts(parts: readonly Message[]): void {
    for (const part of parts) {
      part.close();
    }
  }
}

export class DefaultZLinkActorContext implements ZLinkActorContext {
  readonly boundSession: ZLinkBoundSession;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly joinCoordinator: ZLinkActorJoinCoordinator | undefined,
    boundSessionFactory: ZLinkActorBoundSessionFactory | undefined
  ) {
    this.boundSession = boundSessionFactory?.(state.actorId) ?? new UnboundZLinkSession();
  }

  get spotRid(): RoutingId | undefined {
    return this.state.spotRid;
  }

  get isJoined(): boolean {
    return this.state.isJoined;
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

  joinSpot(spotRid: RoutingId, request?: Message): ZLinkActorJoinSpotCall {
    return new DefaultZLinkActorJoinSpotCall(
      this.state,
      this.requireActor(),
      this.requireJoinCoordinator(),
      spotRid,
      request ?? BindingMessage.from(Buffer.alloc(0)),
      request === undefined
    );
  }

  joinEntrySpot(nodeRid: RoutingId): ZLinkActorJoinEntrySpotCall {
    return new DefaultZLinkActorJoinEntrySpotCall(this.state, this.requireActor(), this.requireJoinCoordinator(), nodeRid);
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
  /**
   * The Entry Spot serial executor. When provided, packets for actors that
   * have not joined a user Spot (the entry path) run through this executor,
   * so Entry Spot actor packet dispatch shares the one Entry Spot serial
   * line with lifecycle, timer and continuation callbacks. User-Spot-joined
   * actors keep the per-actor mailbox dispatch behavior.
   */
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
      if (!snapshot.isJoined && this.options.entryExecutor !== undefined) {
        return this.options.entryExecutor.execute(() => operation(snapshot));
      }
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
    return this.execute(async () => {
      const descriptor = this.requirePacket(ZLinkActorPacketKind.Request, actor, packetName);
      const handler = this.createHandler<ZLinkSpotActorRequestHandler<ZLinkSpot, ZLinkActor, TRequest, TReply>>(descriptor);
      return handler.handle(this.options.spot, actor, this.createRequestContext(packetName, context), request);
    });
  }

  admitActorJoin(
    actor: ZLinkActor,
    request: Message,
    commit: () => Promise<void> | void
  ): Promise<ZLinkSpotActorJoinResponse> {
    return this.execute(async () => {
      const result = await this.options.spot.onActorJoin?.(actor, request) ?? { accepted: false };
      if (!result.accepted) {
        return result;
      }
      await commit();
      await this.options.spot.onJoinActor?.(actor);
      return result;
    });
  }

  notifyJoinActor(actor: ZLinkActor): Promise<void> {
    return this.execute(() => this.options.spot.onJoinActor?.(actor));
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

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly spotRid: RoutingId,
    private readonly request: Message,
    private readonly ownsRequest: boolean
  ) {}

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<ZLinkActorJoinResult> {
    try {
      return await this.coordinator.joinSpot(
        this.actor,
        this.state,
        this.spotRid,
        this.request,
        this.timeoutMs,
        signal
      );
    } finally {
      if (this.ownsRequest) {
        this.request.close();
      }
    }
  }
}

class DefaultZLinkActorJoinEntrySpotCall implements ZLinkActorJoinEntrySpotCall {
  private timeoutMs: number | undefined;

  constructor(
    private readonly state: ZLinkActorRuntimeState,
    private readonly actor: ZLinkActor,
    private readonly coordinator: ZLinkActorJoinCoordinator,
    private readonly nodeRid: RoutingId
  ) {}

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  submit(signal?: AbortSignal): Promise<ActorRef> {
    return this.coordinator.joinEntrySpot(
      this.actor,
      this.state,
      this.nodeRid,
      this.timeoutMs,
      signal
    );
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
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}

function toBackendRoutingId(routingId: RoutingId): ZLinkBackendActorRef['nodeRid'] {
  return routingId as unknown as ZLinkBackendActorRef['nodeRid'];
}

function toFrameworkRoutingId(routingId: ZLinkBackendActorRef['nodeRid']): RoutingId {
  return routingId as unknown as RoutingId;
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
  const created = await resolver?.create?.(type);
  if (created !== undefined) {
    return created;
  }
  const existing = resolver?.get?.(type);
  if (existing !== undefined) {
    return existing;
  }
  return new (type as new () => T)();
}
