import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorDirectory,
  ZLinkActorManager
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
  type ZLinkActorTransferState
} from './actor-transfer-registry';
export {
  ZLINK_REMOTE_ACTOR_JOIN_PACKET
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

export class DefaultZLinkActorManager implements ZLinkActorManager, ZLinkActorDirectory {
  private readonly states = new Map<string, ZLinkActorRuntimeState>();
  private readonly creation: ZLinkActorCreationCoordinator;
  private readonly transferredActorRollback: ZLinkTransferredActorRollbackCoordinator;

  constructor(private readonly options: ZLinkActorManagerOptions) {
    this.creation = new ZLinkActorCreationCoordinator(options);
    this.transferredActorRollback = new ZLinkTransferredActorRollbackCoordinator(this.states, options);
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

  async materializeTransferredActor(
    actorId: string,
    actorType: string,
    adapterKey: string | undefined,
    transferState: ZLinkMessage,
    signal?: AbortSignal
  ): Promise<{ readonly actor: ZLinkActor; readonly actorRef: ActorRef }> {
    throwIfAborted(signal);
    const state = this.getOrCreateState(actorId);
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
    if (state.nativeActorRef === undefined) {
      if (state.actorType !== undefined && state.ownsLocation) {
        await this.options.locationLifecycle?.releaseActor(state.actorType, actor.actorId);
      }
      this.options.actorDestroyedCleanup?.(actor.actorId);
      state.clearAfterDestroy();
      this.states.delete(actor.actorId);
      this.options.metrics?.change('zlink.actor.count', -1);
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
      this.options.metrics?.change('zlink.actor.count', -1);
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
