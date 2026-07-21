import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorDirectory,
  ZLinkActorManager,
  ZLinkActorPlacement
} from '../../contracts';
import {
  ZLinkEncodedPayload,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { Message as BindingMessage, RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
import { ZLinkMessage } from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import { throwIfAborted } from '../abort';
import { routingIdsEqual } from '../routing-id';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';

export {
  DefaultZLinkActorClient,
  forwardEncodedActorPacket,
  type ZLinkActorClientOptions
} from './actor-client';

import {
  encodeFrameworkPayloadMessage
} from '../messaging/payload-codec';
export { DefaultZLinkActorContext } from './actor-context';
import {
  ZLinkActorCreationCoordinator,
  type ZLinkActorCreateRequest
} from './actor-creation';
export {
  ZLinkActorDispatchMailbox,
  ZLinkActorDispatchMailboxSet
} from './actor-mailbox';
export {
  DEFAULT_ACTOR_TRANSFER_FORWARD_WINDOW_MS,
  ZLinkActorHandoffCoordinator,
  decodeHandoffPacket,
  type ZLinkActorHandoffPacket,
  type ZLinkActorHandoffResult,
  type ZLinkActorHandoffTarget
} from './actor-handoff';
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
  type ZLinkActorNativeJoinCoordinatorOptions
} from './actor-remote-joiner';
export type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
export type {
  ZLinkActorBoundSessionFactory,
  ZLinkActorJoinCoordinator,
  ZLinkActorManagerOptions
} from './actor-runtime-contracts';
import type { ZLinkActorManagerOptions } from './actor-runtime-contracts';
import { ZLinkTransferredActorRollbackCoordinator } from './transferred-actor-rollback';
export {
  ZLinkActorTransferRegistry,
  type ZLinkActorTransferPayloadState
} from './actor-transfer-registry';
export {
  ZLINK_REMOTE_ACTOR_JOIN_PACKET,
  ZLINK_REMOTE_ACTOR_SOURCE_LEAVE_TERMINAL,
  decodeRemoteActorSourceLeaveTerminal
} from './actor-remote-wire';
export {
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
  ZLINK_REMOTE_ACTOR_SESSION_DISCONNECTED_PACKET
} from './actor-packet-relay-wire';
export {
  ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET,
  ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET
} from './bound-session-wire';
export {
  createActorJoinRequest,
  createActorMembership,
  ZLINK_ACTOR_LIFECYCLE_SNAPSHOT,
  type ZLinkActorLifecycleSnapshotSource
} from './actor-lifecycle-snapshot';

export class DefaultZLinkActorManager implements ZLinkActorManager, ZLinkActorDirectory {
  private readonly states = new Map<string, ZLinkActorRuntimeState>();
  private readonly actorMeshNames = new Map<string, string>();
  private readonly creation: ZLinkActorCreationCoordinator;
  private readonly transferredActorRollback: ZLinkTransferredActorRollbackCoordinator;

  constructor(private readonly options: ZLinkActorManagerOptions) {
    this.creation = new ZLinkActorCreationCoordinator(options);
    this.transferredActorRollback = new ZLinkTransferredActorRollbackCoordinator(this.states, options);
  }

  create(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  create(
    meshName: string,
    actorId: string,
    actorType: string,
    createRequest: unknown,
    signal?: AbortSignal
  ): Promise<ActorRef>;
  async create(
    meshNameOrActorId: string,
    actorIdOrActorType: string,
    actorTypeOrRequest?: string | AbortSignal | unknown,
    requestOrSignal?: AbortSignal | unknown,
    signal?: AbortSignal
  ): Promise<ActorRef> {
    const legacy = typeof actorTypeOrRequest !== 'string';
    const actorId = legacy ? meshNameOrActorId : actorIdOrActorType;
    const actorType = legacy ? actorIdOrActorType : actorTypeOrRequest;
    const meshName = legacy
      ? this.options.actorMeshNameProvider?.(actorType) ?? 'legacy-actor-mesh'
      : meshNameOrActorId;
    const signalOrRequest = legacy ? actorTypeOrRequest : requestOrSignal;
    const finalSignal = legacy ? requestOrSignal as AbortSignal | undefined : signal;
    requireActorMeshName(meshName);
    this.ensureActorTypeBelongsToMesh(meshName, actorType);
    this.rememberActorMesh(actorId, meshName);
    const args = normalizeCreateRequestArgs(signalOrRequest, finalSignal);
    const result = await this.createOrGet(actorId, actorType, true, args.request, args.signal);
    return result.actorRef;
  }

  find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
  async find(
    meshNameOrActorId: string,
    actorIdOrSignal?: string | AbortSignal,
    signal?: AbortSignal
  ): Promise<ActorRef | undefined> {
    const legacy = typeof actorIdOrSignal !== 'string';
    const actorId = legacy ? meshNameOrActorId : actorIdOrSignal;
    const meshName = legacy
      ? this.actorMeshNames.get(actorId) ?? 'legacy-actor-mesh'
      : meshNameOrActorId;
    const finalSignal = legacy ? actorIdOrSignal : signal;
    requireActorMeshName(meshName);
    return await this.findInMesh(meshName, actorId, finalSignal);
  }

  async findInMesh(
    meshName: string,
    actorId: string,
    signal?: AbortSignal
  ): Promise<ActorRef | undefined> {
    throwIfAborted(signal);
    const state = this.states.get(actorId);
    if (state?.actor !== undefined && this.stateBelongsToMesh(state, meshName)) {
      return this.actorRefForState(state);
    }
    return await this.options.actorRefResolver?.resolveActorRef(meshName, actorId, signal);
  }

  getOrCreate(meshName: string, actorId: string, actorType: string, signal?: AbortSignal): Promise<ActorRef>;
  getOrCreate(
    meshName: string,
    actorId: string,
    actorType: string,
    createRequest: unknown,
    signal?: AbortSignal
  ): Promise<ActorRef>;
  async getOrCreate(
    meshNameOrActorId: string,
    actorIdOrActorType: string,
    actorTypeOrRequest?: string | AbortSignal | unknown,
    requestOrSignal?: AbortSignal | unknown,
    signal?: AbortSignal
  ): Promise<ActorRef> {
    const legacy = typeof actorTypeOrRequest !== 'string';
    const actorId = legacy ? meshNameOrActorId : actorIdOrActorType;
    const actorType = legacy ? actorIdOrActorType : actorTypeOrRequest;
    const meshName = legacy
      ? this.options.actorMeshNameProvider?.(actorType) ?? 'legacy-actor-mesh'
      : meshNameOrActorId;
    const signalOrRequest = legacy ? actorTypeOrRequest : requestOrSignal;
    const finalSignal = legacy ? requestOrSignal as AbortSignal | undefined : signal;
    requireActorMeshName(meshName);
    this.ensureActorTypeBelongsToMesh(meshName, actorType);
    this.rememberActorMesh(actorId, meshName);
    const args = normalizeCreateRequestArgs(signalOrRequest, finalSignal);
    const result = await this.createOrGet(actorId, actorType, false, args.request, args.signal);
    return result.actorRef;
  }

  async materializeTransferredActor(
    actorId: string,
    actorType: string,
    adapterKey: string | undefined,
    transferState: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ActorRef }> {
    throwIfAborted(signal);
    const state = this.getOrCreateState(actorId);
    this.rememberActorMeshFromType(actorId, actorType);
    state.prepareForRemoteReentry();
    const operation = state.getOrStartCreation(
      actorType,
      false,
      () => this.creation.materializeTransferredActor(
        actorId,
        actorType,
        state,
        adapterKey === undefined
          ? undefined
          : () => {
              const registry = this.options.actorTransferRegistry;
              if (registry === undefined) {
                throw new ZLinkConfigurationException('Actor transfer registry is not configured.');
              }
              return registry.transferIn(adapterKey, actorId, transferState, signal);
            }
      )
    );
    try {
      const actor = await operation.task;
      return { actor, actorRef: this.actorRefForState(state) };
    } catch (error) {
      state.clearFailedCreation(operation.task);
      throw error;
    }
  }

  async rollbackTransferredActor(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    await this.transferredActorRollback.rollback(actor, signal);
  }

  async ensure(
    meshName: string,
    actorId: string,
    createRequest: unknown,
    placement?: ZLinkActorPlacement,
    signal?: AbortSignal
  ): Promise<ActorRef> {
    requireActorMeshName(meshName);
    const existing = await this.findInMesh(meshName, actorId, signal);
    if (existing !== undefined) {
      return existing;
    }
    this.ensurePlacementCanBeHostedHere(placement);
    const actorType = actorTypeFromCreateRequest(createRequest);
    this.ensureActorTypeBelongsToMesh(meshName, actorType);
    this.rememberActorMesh(actorId, meshName);
    try {
      const result = await this.createOrGet(actorId, actorType, false, createRequest, signal);
      return result.actorRef;
    } catch (error) {
      if (
        error instanceof ZLinkFrameworkException &&
        error.kind === ZLinkFrameworkErrorKind.ActorCreateFailed
      ) {
        const raced = await this.findInMesh(meshName, actorId, signal);
        if (raced !== undefined) {
          return raced;
        }
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

  private stateBelongsToMesh(state: ZLinkActorRuntimeState, meshName: string): boolean {
    const rememberedMesh = this.actorMeshNames.get(state.actorId);
    if (rememberedMesh !== undefined) return rememberedMesh === meshName;
    const actorType = state.actorType;
    if (actorType === undefined) return false;
    const registeredMesh = this.options.actorMeshNameProvider?.(actorType);
    return registeredMesh === undefined ? meshName.length === 0 : registeredMesh === meshName;
  }

  private ensureActorTypeBelongsToMesh(meshName: string, actorType: string): void {
    const registeredMesh = this.options.actorMeshNameProvider?.(actorType);
    if (registeredMesh !== undefined && registeredMesh !== meshName) {
      throw new ZLinkConfigurationException(
        `Actor type '${actorType}' belongs to RouteMesh '${registeredMesh}', not '${meshName}'.`
      );
    }
  }

  private rememberActorMesh(actorId: string, meshName: string): void {
    const existing = this.actorMeshNames.get(actorId);
    if (existing !== undefined && existing !== meshName) {
      throw new ZLinkConfigurationException(
        `Actor '${actorId}' belongs to RouteMesh '${existing}', not '${meshName}'.`
      );
    }
    this.actorMeshNames.set(actorId, meshName);
  }

  private rememberActorMeshFromType(actorId: string, actorType: string): void {
    const meshName = this.options.actorMeshNameProvider?.(actorType);
    if (meshName !== undefined) {
      this.rememberActorMesh(actorId, meshName);
    }
  }

  private ensurePlacementCanBeHostedHere(placement: ZLinkActorPlacement | undefined): void {
    const preferred = placement?.preferredNodeRid;
    if (preferred === undefined) return;
    const local = this.options.actorCreatedNodeRidProvider?.();
    if (local === undefined || !routingIdsEqual(local, preferred)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RouteNotConnected,
        `Preferred actor node '${preferred}' is not connected to this actor directory.`
      );
    }
  }

  async findActor(actorId: string, signal?: AbortSignal): Promise<ZLinkActor | undefined> {
    throwIfAborted(signal);
    return this.states.get(actorId)?.actor;
  }

  async getOrCreateActor(actorId: string, actorType: string, signal?: AbortSignal): Promise<ZLinkActor> {
    this.rememberActorMeshFromType(actorId, actorType);
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
    this.rememberActorMeshFromType(actorId, actorType);
    const state = this.getOrCreateState(actorId);
    state.setNativeActorRef(actorRef);
    const result = await this.createOrGet(actorId, actorType, false, createRequest, signal, false);
    return result.actor;
  }

  getState(actorId: string): ZLinkActorRuntimeState | undefined {
    return this.states.get(actorId);
  }

  snapshotStates(): readonly ZLinkActorRuntimeState[] {
    return [...this.states.values()];
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
    const destroyTask = state.getOrStartDestroy(entryNodeRid, async (actorRef) => {
      if (actorRef !== undefined) {
        await node.destroyActor(actorRef, 0, destroySignal);
        state.markNativeActorDestroyed(actorRef);
      }
      if (state.actorType !== undefined && state.ownsLocation) {
        await this.options.locationLifecycle?.releaseActor(state.actorType, actor.actorId);
      }
      this.options.actorDestroyedCleanup?.(actor.actorId);
      state.clearAfterDestroy();
      if (this.states.get(actor.actorId) === state) {
        this.states.delete(actor.actorId);
        this.actorMeshNames.delete(actor.actorId);
      }
      this.options.metrics?.change('zlink.actor.count', -1);
    });

    try {
      await destroyTask;
    } catch (error) {
      state.clearFailedDestroy(destroyTask);
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
    const existingState = this.states.get(actorId);
    if (existingState?.hasActorOrCreation !== true) {
      this.options.admission?.requireActorCreate(
        actorId,
        this.options.actorMeshNameProvider?.(actorType)
      );
    }
    const state = existingState ?? this.getOrCreateState(actorId);
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
    this.options.metrics?.change('zlink.actor.count', 1);
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

function requireActorMeshName(meshName: string): void {
  if (meshName.length === 0 || meshName !== meshName.trim()) {
    throw new ZLinkConfigurationException(
      'Actor directory RouteMesh name must not be empty or padded.'
    );
  }
}
