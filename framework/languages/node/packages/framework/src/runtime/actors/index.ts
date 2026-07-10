import type {
  ActorRef,
  RoutingId,
  Type,
  ZLinkProviderResolver,
  ZLinkActor,
  ZLinkActorDirectory,
  ZLinkActorFactory,
  ZLinkActorJoinResult,
  ZLinkActorManager,
  ZLinkBoundSession,
  ZLinkSpot
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import {
  ZLinkEncodedPayload,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { Message as BindingMessage, RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import { ZLinkMessage, type ZLinkMessageSerializer } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';

export {
  DefaultZLinkActorClient,
  type ZLinkActorClientOptions
} from './actor-client';

import {
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
import type { ZLinkLocationLifecycle } from '../locations';
export { DefaultZLinkActorContext } from './actor-context';
import {
  ZLinkActorCreationCoordinator,
  type ZLinkActorCreateRequest
} from './actor-creation';
export {
  ZLinkActorDispatchMailbox,
  ZLinkActorDispatchMailboxSet
} from './actor-mailbox';
import { ZLinkActorDispatchMailboxSet } from './actor-mailbox';
export {
  ZLinkActorRuntimeState,
  toFrameworkActorRef,
  toFrameworkRoutingId,
  type ZLinkActorCreationOperation,
  type ZLinkRemoteActorPacketTarget,
  type ZLinkRemoteBoundSessionTarget
} from './actor-runtime-state';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef
} from './actor-runtime-state';
export {
  DefaultZLinkSpotActorReplyOptions,
  ZLinkActorPacketKind,
  ZLinkSpotActorDispatcher,
  ZLinkSpotActorHandlerRegistryRuntime,
  type ZLinkActorPacketDescriptor,
  type ZLinkSpotActorDispatcherOptions,
  type ZLinkSpotActorReplyOptionsSnapshot
} from './spot-actor-dispatch';
export {
  ZLinkActorNativeJoinCoordinator,
  type ZLinkActorNativeJoinCoordinatorOptions,
  type ZLinkActorRoutedJoinTransport
} from './actor-remote-joiner';
export {
  ZLINK_REMOTE_ACTOR_JOIN_PACKET,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from './actor-remote-wire';

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

export class DefaultZLinkActorManager implements ZLinkActorManager, ZLinkActorDirectory {
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

  async ensure(
    actorId: string,
    createRequest: unknown,
    _placement?: unknown,
    signal?: AbortSignal
  ): Promise<ActorRef> {
    const actorType = actorTypeFromCreateRequest(createRequest);
    try {
      const result = await this.createOrGet(actorId, actorType, false, createRequest, signal);
      return result.actorRef;
    } catch (error) {
      if (
        error instanceof ZLinkFrameworkException &&
        error.kind === ZLinkFrameworkErrorKind.ActorCreateFailed
      ) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorCreateRejected,
          `Actor '${actorId}' create request was rejected.`,
          false,
          error
        );
      }
      throw error;
    }
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
    _options: ZLinkActorDispatchRouterOptions = {}
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

function actorTypeFromCreateRequest(createRequest: unknown): string {
  if (typeof createRequest === 'object' && createRequest !== null) {
    const actorType = (createRequest as { actorType?: unknown }).actorType;
    if (typeof actorType === 'string' && actorType.length > 0) {
      return actorType;
    }
  }
  throw new ZLinkConfigurationException(
    'Actor directory ensure requires createRequest.actorType so actor type is supplied only for creation.'
  );
}
