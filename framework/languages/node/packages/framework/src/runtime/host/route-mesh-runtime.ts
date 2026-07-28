import type { RoutingId } from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMeshNodeState,
  ZLinkPeerState,
  type ZLinkMeshNodeSnapshot,
  type ZLinkMeshRuntimeEvent,
  type ZLinkRouteMeshRuntime
} from '../../contracts';

type ZLinkDrainForceReason =
  | 'deadline_exceeded'
  | 'drain_state_publish_failed'
  | 'owner_cleanup_failed'
  | 'teardown_failed';

type ZLinkMeshDrainResult =
  | { readonly kind: 'drained' }
  | { readonly kind: 'forceStopped'; readonly reason: ZLinkDrainForceReason };
import type { ZLinkBackendMeshNode } from '../backend';
import type { ZLinkRuntimeAdmissionGate } from '../admission';
import type { ZLinkSpotNodeOptions } from '../configuration';
import {
  ZLinkObjectRole,
  type ZLinkMeshNodeDescriptor
} from '../../contracts';

export interface ZLinkRouteMeshRuntimeCoordinatorOptions {
  readonly meshNames: readonly string[];
  readonly meshOptions: ReadonlyMap<string, ZLinkSpotNodeOptions>;
  readonly meshNode: (meshName: string) => ZLinkBackendMeshNode | undefined;
  readonly meshNodeDescriptor?: (
    meshName: string
  ) => ZLinkMeshNodeDescriptor | undefined;
  readonly admission: ZLinkRuntimeAdmissionGate;
  readonly publishRetiring: (meshName: string, signal: AbortSignal) => Promise<void>;
  readonly rollbackRetiring: (meshName: string, signal: AbortSignal) => Promise<void>;
  readonly publishDraining: (meshName: string, signal: AbortSignal) => Promise<void>;
  readonly publishHostDraining: (signal: AbortSignal) => Promise<void>;
  readonly drainResources: (meshName: string, signal: AbortSignal) => Promise<void>;
  readonly cleanupHostResources: (signal: AbortSignal) => Promise<void>;
  readonly forceStopResources: (meshName: string) => Promise<void>;
}

interface ZLinkMeshDrainState {
  state: ZLinkMeshNodeState;
  sequence: bigint;
  deadline?: Date;
  operation?: Promise<ZLinkMeshDrainResult>;
  result?: ZLinkMeshDrainResult;
  readonly waiters: Array<(result: ZLinkMeshDrainResult) => void>;
  readonly observers: Set<ZLinkMeshEventQueue>;
}

export class ZLinkRouteMeshRuntimeCoordinator implements ZLinkRouteMeshRuntime {
  private readonly states = new Map<string, ZLinkMeshDrainState>();
  private hostOperation?: Promise<ZLinkMeshDrainResult>;
  private hostRetiringPrepared = false;

  constructor(private readonly options: ZLinkRouteMeshRuntimeCoordinatorOptions) {
    for (const meshName of options.meshNames) {
      options.admission.register(meshName);
      this.states.set(meshName, {
        state: ZLinkMeshNodeState.Starting,
        sequence: 0n,
        waiters: [],
        observers: new Set()
      });
    }
  }

  markServing(): void {
    for (const [meshName, state] of this.states) {
      if (state.state !== ZLinkMeshNodeState.Starting) continue;
      this.transition(meshName, state, ZLinkMeshNodeState.Serving);
    }
  }

  snapshot(meshName: string): ZLinkMeshNodeSnapshot {
    const drain = this.requireState(meshName);
    const node = this.options.meshNode(meshName);
    if (node === undefined) throw routeNotFound(meshName);
    const status = node.status();
    const descriptor = this.options.meshNodeDescriptor?.(meshName);
    const backendPeers = node.peers();
    const peerChannels = backendPeers.map((peer) => peer.routingId === null
      ? { names: [] as readonly string[], weights: [] as readonly number[] }
      : node.peerChannels(peer.routingId, peer.lifecycleGeneration));
    const peers = backendPeers.map((peer, index) => ({
      rid: (peer.routingId === null ? '' : String(peer.routingId)) as RoutingId,
      lifecycleGeneration: peer.lifecycleGeneration,
      descriptorRevision: peer.descriptorRevision,
      endpoint: peer.endpoint,
      state: peerState(peer.state),
      ready: peer.state === 3 && peer.routingId !== null,
      drainState: peer.state === 4 ? 'draining' : 'active',
      channelNames: [...(peerChannels[index]?.names ?? [])],
      lastFailure: peer.lastError === 0 ? undefined : String(peer.lastError)
    }));
    const channels = Object.entries(this.options.meshOptions.get(meshName)?.meshChannels ?? {})
      .map(([channelName, channel]) => {
        const readyMemberCount = BigInt(peerChannels.filter((entry, index) =>
          peers[index]?.ready === true
          && entry.names.some((name, channelIndex) =>
            name === channelName && (entry.weights[channelIndex] ?? 0) > 0)
        ).length);
        const localWeight = channel.weight ?? 1;
        return { channelName, localWeight, readyMemberCount, selectable: localWeight > 0 || readyMemberCount > 0n };
      });
    const pendingApplicationWork = BigInt(this.options.admission.pending(meshName));
    return {
      meshName,
      rid: String(status.routingId),
      lifecycleGeneration: status.lifecycleGeneration,
      descriptorRevision: status.descriptorRevision,
      endpoint: status.localEndpoint,
      objectRole: descriptor?.objectRole ?? ZLinkObjectRole.None,
      placementWeight: descriptor?.placementWeight ?? 0,
      populationCapacity: descriptor?.populationCapacity ?? {
        actors: { active: 0, reserved: 0, limit: 0 },
        spots: { active: 0, reserved: 0, limit: 0 },
        spotTypes: []
      },
      activationConcurrency: descriptor?.activationConcurrency ?? {
        active: 0,
        limit: 1
      },
      applicationVersion: descriptor?.applicationVersion ?? 0n,
      placementReservationFailureCount: 0n,
      objectCapabilities: descriptor?.objectCapabilities ?? [],
      state: drain.state === ZLinkMeshNodeState.Serving ? backendState(status.state) : drain.state,
      sequence: drain.sequence > status.lastChangedMs ? drain.sequence : status.lastChangedMs,
      observedAt: new Date(),
      descriptorSources: [],
      peers,
      channels,
      instanceSpots: [],
      claims: {
        applicationActive: pendingApplicationWork > 0n,
        pendingApplicationWork,
        infrastructureActive: status.pendingInfrastructureMessages > 0n,
        pendingInfrastructureWork: status.pendingInfrastructureMessages
      },
      location: { state: 'unknown' }
    };
  }

  observe(meshName: string, capacity = 64, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent> {
    const state = this.requireState(meshName);
    if (!Number.isInteger(capacity) || capacity <= 0) throw new RangeError('Observer capacity must be positive.');
    const queue = new ZLinkMeshEventQueue(capacity, signal, () => state.observers.delete(queue));
    state.observers.add(queue);
    return queue;
  }

  isReady(meshName: string): boolean {
    const state = this.requireState(meshName);
    return state.state === ZLinkMeshNodeState.Serving && this.options.meshNode(meshName) !== undefined;
  }

  drain(meshName: string, deadlineMs = 30_000, signal?: AbortSignal): Promise<ZLinkMeshDrainResult> {
    const state = this.requireState(meshName);
    if (this.states.size > 1) return Promise.reject(multiMeshDrainError());
    if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
      return Promise.reject(new TypeError('Drain deadlineMs must be greater than zero.'));
    }
    state.operation ??= this.performDrain(meshName, state, deadlineMs);
    return waitForOperation(state.operation, signal);
  }

  awaitDrained(meshName: string, signal?: AbortSignal): Promise<ZLinkMeshDrainResult> {
    const state = this.requireState(meshName);
    if (this.states.size > 1) return Promise.reject(multiMeshDrainError());
    const operation = state.operation ?? new Promise<ZLinkMeshDrainResult>((resolve) => state.waiters.push(resolve));
    return waitForOperation(operation, signal);
  }

  drainHost(deadlineMs = 30_000, signal?: AbortSignal): Promise<ZLinkMeshDrainResult> {
    if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) {
      return Promise.reject(new TypeError('Drain deadlineMs must be greater than zero.'));
    }
    if (this.hostOperation === undefined) {
      const onlyState = this.states.size === 1 ? this.states.values().next().value : undefined;
      const operation = onlyState?.operation ?? this.performHostDrain(deadlineMs);
      this.hostOperation = operation;
      for (const state of this.states.values()) state.operation ??= operation;
    }
    return waitForOperation(this.hostOperation, signal);
  }

  async prepareHostRetire(
    deadlineMs: number
  ): Promise<'prepared' | 'store_unavailable' | 'deadline_exceeded'> {
    if (this.hostRetiringPrepared) return 'prepared';
    const deadline = new AbortController();
    const timer = setTimeout(
      () => deadline.abort(new Error('Retire descriptor publication deadline exceeded.')),
      deadlineMs
    );
    const attempted: string[] = [];
    try {
      for (const meshName of this.states.keys()) {
        attempted.push(meshName);
        await this.options.publishRetiring(meshName, deadline.signal);
      }
      this.hostRetiringPrepared = true;
      return 'prepared';
    } catch {
      // A failed response can still follow a committed Store write. Restore
      // every attempted descriptor before the host reports a reversible block.
      const rollback = new AbortController();
      const rollbackTimer = setTimeout(
        () => rollback.abort(new Error('Retire descriptor rollback deadline exceeded.')),
        Math.min(deadlineMs, 1000)
      );
      let rollbackFailed = false;
      for (const meshName of attempted.reverse()) {
        try {
          await this.options.rollbackRetiring(meshName, rollback.signal);
        } catch {
          rollbackFailed = true;
        }
      }
      clearTimeout(rollbackTimer);
      if (rollbackFailed) {
        throw new ZLinkRetiringRollbackError();
      }
      return deadline.signal.aborted ? 'deadline_exceeded' : 'store_unavailable';
    } finally {
      clearTimeout(timer);
    }
  }

  private async performDrain(
    meshName: string,
    state: ZLinkMeshDrainState,
    deadlineMs: number
  ): Promise<ZLinkMeshDrainResult> {
    state.deadline = new Date(Date.now() + deadlineMs);
    this.options.admission.seal(meshName);
    this.transition(meshName, state, ZLinkMeshNodeState.Draining);
    const deadline = new AbortController();
    const timer = setTimeout(() => deadline.abort(new Error('Drain deadline exceeded.')), deadlineMs);
    let result: ZLinkMeshDrainResult;
    try {
      await this.options.publishDraining(meshName, deadline.signal);
      await this.options.publishHostDraining(deadline.signal);
      await this.options.admission.awaitZero(meshName, deadline.signal);
      await this.options.drainResources(meshName, deadline.signal);
      await this.options.cleanupHostResources(deadline.signal);
      result = { kind: 'drained' };
      this.transition(meshName, state, ZLinkMeshNodeState.Drained);
    } catch (error) {
      const classified = drainFailureReason(error);
      const reason: ZLinkDrainForceReason = classified !== 'teardown_failed'
        ? classified
        : deadline.signal.aborted ? 'deadline_exceeded' : classified;
      await this.options.forceStopResources(meshName).catch(() => undefined);
      result = { kind: 'forceStopped', reason };
      this.transition(meshName, state, ZLinkMeshNodeState.ForceStopping, reason);
    } finally {
      clearTimeout(timer);
    }
    state.result = result;
    for (const resolve of state.waiters.splice(0)) resolve(result);
    return result;
  }

  private async performHostDrain(deadlineMs: number): Promise<ZLinkMeshDrainResult> {
    const entries = [...this.states.entries()];
    if (entries.length === 0) return { kind: 'drained' };
    const deadlineAt = new Date(Date.now() + deadlineMs);
    for (const [, state] of entries) {
      state.deadline = deadlineAt;
    }
    const deadline = new AbortController();
    const timer = setTimeout(() => deadline.abort(new Error('Drain deadline exceeded.')), deadlineMs);
    let result: ZLinkMeshDrainResult;
    try {
      if (!this.hostRetiringPrepared) {
        await Promise.all(entries.map(([meshName]) =>
          this.options.publishRetiring(meshName, deadline.signal)));
      }
      this.hostRetiringPrepared = false;
      await Promise.all(entries.map(([meshName]) =>
        this.options.drainResources(meshName, deadline.signal)));
      for (const [meshName, state] of entries) {
        this.options.admission.seal(meshName);
        this.transition(meshName, state, ZLinkMeshNodeState.Draining);
      }
      await Promise.all(entries.map(([meshName]) =>
        this.options.publishDraining(meshName, deadline.signal)));
      await this.options.publishHostDraining(deadline.signal);
      await Promise.all(entries.map(([meshName]) =>
        this.options.admission.awaitZero(meshName, deadline.signal)));
      await this.options.cleanupHostResources(deadline.signal);
      result = { kind: 'drained' };
      for (const [meshName, state] of entries) {
        this.transition(meshName, state, ZLinkMeshNodeState.Drained);
      }
    } catch (error) {
      const classified = drainFailureReason(error);
      const reason: ZLinkDrainForceReason = classified !== 'teardown_failed'
        ? classified
        : deadline.signal.aborted ? 'deadline_exceeded' : classified;
      await Promise.all(entries.map(([meshName]) =>
        this.options.forceStopResources(meshName).catch(() => undefined)));
      result = { kind: 'forceStopped', reason };
      for (const [meshName, state] of entries) {
        this.transition(meshName, state, ZLinkMeshNodeState.ForceStopping, reason);
      }
    } finally {
      clearTimeout(timer);
    }
    for (const [, state] of entries) {
      state.result = result;
      for (const resolve of state.waiters.splice(0)) resolve(result);
    }
    return result;
  }

  private transition(
    meshName: string,
    state: ZLinkMeshDrainState,
    next: ZLinkMeshNodeState,
    reason?: string
  ): void {
    if (state.state === next) return;
    state.state = next;
    state.sequence += 1n;
    const node = this.options.meshNode(meshName);
    const nodeStatus = node?.status();
    const event: ZLinkMeshRuntimeEvent = {
      identifier: 'zlink.runtime.mesh_node.drain_changed',
      sequence: state.sequence,
      timestamp: new Date(),
      meshName,
      sourceRid: String(nodeStatus?.routingId ?? '') as RoutingId,
      state: next,
      reason
    };
    for (const observer of state.observers) observer.push(event);
    if (next === ZLinkMeshNodeState.Drained || next === ZLinkMeshNodeState.ForceStopping) {
      for (const observer of state.observers) observer.close();
      state.observers.clear();
    }
  }

  private requireState(meshName: string): ZLinkMeshDrainState {
    const state = this.states.get(meshName);
    if (state !== undefined) return state;
    throw routeNotFound(meshName);
  }
}

export class ZLinkRetiringRollbackError extends Error {
  constructor() {
    super('Retiring descriptor publication could not be rolled back to Serving.');
    this.name = 'ZLinkRetiringRollbackError';
  }
}

class ZLinkMeshEventQueue implements AsyncIterable<ZLinkMeshRuntimeEvent>, AsyncIterator<ZLinkMeshRuntimeEvent> {
  private readonly values: ZLinkMeshRuntimeEvent[] = [];
  private readonly waiters: Array<(result: IteratorResult<ZLinkMeshRuntimeEvent>) => void> = [];
  private closed = false;

  constructor(
    private readonly capacity: number,
    signal: AbortSignal | undefined,
    private readonly remove: () => void
  ) {
    signal?.addEventListener('abort', () => this.close(), { once: true });
  }

  [Symbol.asyncIterator](): AsyncIterator<ZLinkMeshRuntimeEvent> { return this; }

  next(): Promise<IteratorResult<ZLinkMeshRuntimeEvent>> {
    const value = this.values.shift();
    if (value !== undefined) return Promise.resolve({ done: false, value });
    if (this.closed) return Promise.resolve({ done: true, value: undefined });
    return new Promise((resolve) => this.waiters.push(resolve));
  }

  return(): Promise<IteratorResult<ZLinkMeshRuntimeEvent>> {
    this.close();
    return Promise.resolve({ done: true, value: undefined });
  }

  push(value: ZLinkMeshRuntimeEvent): void {
    if (this.closed) return;
    const waiter = this.waiters.shift();
    if (waiter !== undefined) {
      waiter({ done: false, value });
      return;
    }
    if (this.values.length === this.capacity) this.values.shift();
    this.values.push(value);
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    this.remove();
    for (const resolve of this.waiters.splice(0)) resolve({ done: true, value: undefined });
  }
}

function routeNotFound(meshName: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RouteNotConnected,
    `RouteMesh '${meshName}' is not registered or no longer available.`
  );
}

function multiMeshDrainError(): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.RequestRejected,
    'RouteMesh drain is unavailable when one framework host owns multiple RouteMesh instances.'
  );
}

function backendState(state: number): ZLinkMeshNodeState {
  switch (state) {
    case 1: return ZLinkMeshNodeState.Starting;
    case 2:
    case 3:
    case 4: return ZLinkMeshNodeState.Serving;
    case 5: return ZLinkMeshNodeState.Draining;
    case 6: return ZLinkMeshNodeState.Stopped;
    default: return ZLinkMeshNodeState.Faulted;
  }
}

function peerState(state: number): ZLinkPeerState {
  switch (state) {
    case 3: return ZLinkPeerState.Ready;
    case 4: return ZLinkPeerState.Draining;
    case 6: return ZLinkPeerState.NotRequired;
    case 1:
    case 2:
      return ZLinkPeerState.Connecting;
    default:
      return ZLinkPeerState.NotConnected;
  }
}

function drainFailureReason(error: unknown): ZLinkDrainForceReason {
  const name = error instanceof Error ? error.name : '';
  if (name === 'ZLinkDrainingStatePublishError') return 'drain_state_publish_failed';
  if (name === 'ZLinkOwnerCleanupError' || error instanceof AggregateError
      && error.errors.some((nested) => nested instanceof Error && nested.name === 'ZLinkOwnerCleanupError')) {
    return 'owner_cleanup_failed';
  }
  return 'teardown_failed';
}

function waitForOperation<T>(operation: Promise<T>, signal?: AbortSignal): Promise<T> {
  if (signal === undefined) return operation;
  if (signal.aborted) return Promise.reject(signal.reason);
  return new Promise<T>((resolve, reject) => {
    const abort = () => reject(signal.reason);
    signal.addEventListener('abort', abort, { once: true });
    operation.then(
      (result) => {
        signal.removeEventListener('abort', abort);
        resolve(result);
      },
      (error) => {
        signal.removeEventListener('abort', abort);
        reject(error);
      }
    );
  });
}
