import { createHash } from 'node:crypto';
import type {
  ZLinkAuthorityKey,
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStore,
  ZLinkObjectCreationStore
} from '../../contracts/Locations/Authority';
import type {
  ZLinkRelocationReference,
  ZLinkRelocationStore
} from '../../contracts/Locations/RelocationStore';
import type { ZLinkBackendMeshNode } from '../backend/contracts';
import type {
  ServicePendingInstanceActivation
} from '../foundation/service-stateful-runtime';
import type {
  ServiceInstanceRouteFence,
  ServiceSpotRouteFence
} from '../foundation/service-stateful-wire-codec';
import { routingIdsEqual } from '../routing-id';
import {
  decodeServiceInstanceAuthorityPayload,
  decodeServiceReadySpotAuthority,
  type ServiceActivationRecoveryState
} from '../foundation/service-authority-payload-codec';
import {
  decodeInstanceActivationRecoveryEnvelope,
  type ServiceInstanceActivationRecoveryEnvelope
} from '../foundation/service-instance-activation-recovery-codec';

interface StatefulAuthorityRouteSink {
  status(): ReturnType<ZLinkBackendMeshNode['status']>;
  rememberSpotRoute(route: ServiceSpotRouteFence, storeVersion: string): void;
  forgetSpotRoute(
    spot: ServiceSpotRouteFence['spot'],
    authorityOwnerGeneration: bigint,
    storeVersion: string
  ): void;
  registerInstanceIntent(instanceType: string, route: ServiceInstanceRouteFence): void;
  forgetInstanceIntent(
    spotRid: string,
    authorityOwnerGeneration: bigint,
    storeVersion: string
  ): void;
  recoverInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    route: ServiceInstanceRouteFence
  ): Promise<void>;
  recoverPendingInstanceActivation(
    envelope: ServiceInstanceActivationRecoveryEnvelope,
    pending: ServicePendingInstanceActivation
  ): Promise<void>;
  completeRecoveredInstanceActivation(
    target: ServiceInstanceActivationRecoveryEnvelope['target'],
    route: ServiceInstanceRouteFence
  ): Promise<ServiceInstanceRouteFence>;
}

interface AppliedAuthorityRoute {
  readonly kind: 'user_spot' | 'instance_spot';
  readonly stableType: string;
  readonly meshName: string;
  readonly spotRoute: ServiceSpotRouteFence;
  readonly instanceRoute: ServiceInstanceRouteFence;
  readonly activationRecovery?: ServiceActivationRecoveryState;
}

interface PendingInstanceActivationRecovery {
  readonly targetMeshName: string;
  readonly targetNodeRid: string;
  readonly targetNodeGeneration: bigint;
  readonly stableType: string;
  readonly spotRid: string;
  readonly pending: ServicePendingInstanceActivation;
}

interface CompleteAuthoritySnapshot {
  readonly routes: Map<string, AppliedAuthorityRoute>;
  readonly pending: readonly PendingInstanceActivationRecovery[];
}

export interface ZLinkStatefulAuthorityRouteRuntimeOptions {
  readonly store: ZLinkAuthorityStore;
  readonly creationStore?: ZLinkObjectCreationStore;
  readonly relocationStore?: ZLinkRelocationStore;
  readonly meshNodes: ReadonlyMap<string, ZLinkBackendMeshNode>;
  readonly pollingIntervalMs: number;
  readonly pageSize: number;
  readonly reportError: (error: unknown) => void;
}

/**
 * Reconciles durable Spot authority into the raw service runtime. The Location
 * Store remains authoritative; this object only maintains fenced routing cache.
 */
export class ZLinkStatefulAuthorityRouteRuntime {
  private readonly abortController = new AbortController();
  private readonly appliedByMesh = new Map<string, Map<string, AppliedAuthorityRoute>>();
  private loop?: Promise<void>;

  constructor(private readonly options: ZLinkStatefulAuthorityRouteRuntimeOptions) {}

  async start(parentSignal?: AbortSignal): Promise<void> {
    if (this.loop !== undefined) return;
    if (parentSignal?.aborted === true) throw parentSignal.reason;
    const onParentAbort = () => this.abortController.abort(parentSignal?.reason);
    parentSignal?.addEventListener('abort', onParentAbort, { once: true });
    try {
      await this.reconcile(this.abortController.signal, true);
      this.loop = this.run(this.abortController.signal).finally(() => {
        parentSignal?.removeEventListener('abort', onParentAbort);
      });
    } catch (error) {
      parentSignal?.removeEventListener('abort', onParentAbort);
      throw error;
    }
  }

  async stop(): Promise<void> {
    this.abortController.abort();
    await this.loop;
    this.loop = undefined;
    this.appliedByMesh.clear();
  }

  async reconcile(signal?: AbortSignal, requireComplete = false): Promise<void> {
    const snapshot = await this.readCompleteSnapshot(signal);
    if (snapshot === undefined) {
      if (requireComplete) {
        throw new Error(
          'The initial authority recovery scan expired before a complete snapshot was read.'
        );
      }
      return;
    }
    const current = snapshot.routes;

    const sinks: Array<{
      readonly meshName: string;
      readonly sink: StatefulAuthorityRouteSink;
    }> = [];
    for (const [meshName, node] of this.options.meshNodes) {
      const sink = asStatefulAuthorityRouteSink(node);
      if (sink === undefined) continue;
      sinks.push({ meshName, sink });

      const status = sink.status();
      for (const pending of snapshot.pending) {
        if (
          pending.targetMeshName === meshName
          && routingIdsEqual(status.routingId, pending.targetNodeRid)
          && status.lifecycleGeneration === pending.targetNodeGeneration
        ) {
          await this.recoverPendingInstanceActivation(sink, pending, signal);
        }
      }

      for (const [key, route] of current) {
        if (route.meshName !== meshName) continue;
        sink.rememberSpotRoute(route.spotRoute, route.instanceRoute.storeVersion);
        if (route.kind === 'instance_spot') {
          const status = sink.status();
          if (
            routingIdsEqual(status.routingId, route.instanceRoute.targetNodeRid)
            && status.lifecycleGeneration === route.instanceRoute.targetNodeGeneration
          ) {
            if (route.activationRecovery !== undefined) {
              if (
                route.activationRecovery.replayCursor
                === route.activationRecovery.inboxSequence
              ) {
                const released = await sink.completeRecoveredInstanceActivation(
                  {
                    targetSpotRid: route.instanceRoute.targetSpotRid,
                    stableType: route.stableType,
                    targetNodeRid: route.instanceRoute.targetNodeRid,
                    targetNodeGeneration: route.instanceRoute.targetNodeGeneration,
                    descriptorVersion: status.descriptorRevision.toString()
                  },
                  route.instanceRoute
                );
                const { activationRecovery: _, ...routeWithoutRecovery } = route;
                const completed: AppliedAuthorityRoute = {
                  ...routeWithoutRecovery,
                  instanceRoute: released
                };
                current.set(key, completed);
                sink.rememberSpotRoute(completed.spotRoute, released.storeVersion);
                sink.registerInstanceIntent(route.stableType, released);
                continue;
              } else {
                const recovered = await this.recoverInstanceActivation(
                  sink,
                  route,
                  route.activationRecovery,
                  signal
                );
                if (!recovered) {
                  sink.forgetSpotRoute(
                    route.spotRoute.spot,
                    route.instanceRoute.authorityOwnerGeneration,
                    route.instanceRoute.storeVersion
                  );
                  continue;
                }
              }
            }
            sink.registerInstanceIntent(route.stableType, route.instanceRoute);
          }
        }
      }
    }

    // Publish every successor before cleaning old entries. Conditional cleanup
    // then cannot remove a route or intent installed for a newer generation.
    for (const { meshName, sink } of sinks) {
      const previous = this.appliedByMesh.get(meshName) ?? new Map();
      const meshCurrent = new Map(
        [...current].filter(([, route]) => route.meshName === meshName)
      );
      for (const [key, oldRoute] of previous) {
        const next = meshCurrent.get(key);
        if (oldRoute.meshName === meshName && !sameAppliedRoute(oldRoute, next)) {
          sink.forgetSpotRoute(
            oldRoute.spotRoute.spot,
            oldRoute.spotRoute.authorityOwnerGeneration,
            oldRoute.instanceRoute.storeVersion
          );
          if (oldRoute.kind === 'instance_spot' && oldRoute.meshName === meshName) {
            sink.forgetInstanceIntent(
              oldRoute.instanceRoute.targetSpotRid,
              oldRoute.instanceRoute.authorityOwnerGeneration,
              oldRoute.instanceRoute.storeVersion
            );
          }
        }
      }
      this.appliedByMesh.set(meshName, meshCurrent);
    }
  }

  private async recoverInstanceActivation(
    sink: StatefulAuthorityRouteSink,
    route: AppliedAuthorityRoute,
    recovery: ServiceActivationRecoveryState,
    signal?: AbortSignal
  ): Promise<boolean> {
    const envelope = await this.readRecoveryEnvelope(recovery, signal);
    const status = sink.status();
    if (
      envelope.targetMeshName !== route.meshName
      || envelope.target.stableType !== route.stableType
      || envelope.target.targetSpotRid !== route.instanceRoute.targetSpotRid
      || envelope.target.targetNodeRid !== route.instanceRoute.targetNodeRid
      || envelope.target.targetNodeGeneration !== route.instanceRoute.targetNodeGeneration
      || envelope.target.descriptorVersion !== status.descriptorRevision.toString()
    ) {
      this.options.reportError(
        new Error('Instance activation recovery envelope does not match Ready authority.')
      );
      return false;
    }
    await sink.recoverInstanceActivation(envelope, route.instanceRoute);
    return true;
  }

  private async recoverPendingInstanceActivation(
    sink: StatefulAuthorityRouteSink,
    recovery: PendingInstanceActivationRecovery,
    signal?: AbortSignal
  ): Promise<void> {
    const envelope = await this.readRecoveryEnvelope({
      reference: recovery.pending.requestReference,
      sha256: recovery.pending.requestSha256,
      encodedSize: Number(recovery.pending.requestEncodedSize),
      inboxSequence: 1n,
      replayCursor: 0n
    }, signal);
    const status = sink.status();
    if (
      envelope.targetMeshName !== recovery.targetMeshName
      || envelope.target.stableType !== recovery.stableType
      || envelope.target.targetSpotRid !== recovery.spotRid
      || envelope.target.targetNodeRid !== recovery.targetNodeRid
      || envelope.target.targetNodeGeneration !== recovery.targetNodeGeneration
      || envelope.target.descriptorVersion !== status.descriptorRevision.toString()
    ) {
      const creationStore = this.options.creationStore;
      if (creationStore === undefined) {
        this.options.reportError(
          new Error('Pending Instance activation mismatch requires an object creation store.')
        );
        return;
      }
      const aborted = await creationStore.abort({
        key: { kind: 'instance_spot', globalId: recovery.spotRid },
        reservationId: recovery.pending.reservationId,
        expectedStoreVersion: recovery.pending.storeVersion,
        target: {
          meshName: recovery.pending.meshName,
          nodeRid: recovery.pending.nodeRid as never,
          nodeLifecycleGeneration: recovery.pending.nodeGeneration,
          owner: {
            ownerId: recovery.pending.ownerId,
            leaseGeneration: recovery.pending.ownerLeaseGeneration
          }
        }
      }, signal);
      if (aborted.kind === 'aborted' || aborted.kind === 'alreadyAborted') {
        try {
          await this.options.relocationStore?.deleteRelocation({
            value: recovery.pending.requestReference
          } as ZLinkRelocationReference);
        } catch {
          // The authority no longer publishes this root. Retention owns a
          // failed best-effort orphan cleanup.
        }
      }
      return;
    }
    await sink.recoverPendingInstanceActivation(envelope, recovery.pending);
  }

  private async readRecoveryEnvelope(
    recovery: ServiceActivationRecoveryState,
    signal?: AbortSignal
  ): Promise<ServiceInstanceActivationRecoveryEnvelope> {
    const relocationStore = this.options.relocationStore;
    if (relocationStore === undefined) {
      throw new Error('Instance activation recovery requires a Relocation Store.');
    }
    const result = await relocationStore.getRelocation(
      { value: recovery.reference } as ZLinkRelocationReference,
      signal
    );
    if (result.kind !== 'found') {
      throw new Error(
        `Instance activation recovery payload '${recovery.reference}' is missing.`
      );
    }
    const payload = Buffer.from(result.payload);
    const sha256 = createHash('sha256').update(payload).digest();
    if (
      payload.byteLength !== recovery.encodedSize
      || !sha256.equals(Buffer.from(recovery.sha256))
    ) {
      throw new Error(
        `Instance activation recovery payload '${recovery.reference}' failed integrity validation.`
      );
    }
    return decodeInstanceActivationRecoveryEnvelope(payload);
  }

  private async run(signal: AbortSignal): Promise<void> {
    while (!signal.aborted) {
      try {
        await wait(this.options.pollingIntervalMs, signal);
        await this.reconcile(signal);
      } catch (error) {
        if (aborted(signal)) return;
        this.options.reportError(error);
      }
    }
  }

  private async readCompleteSnapshot(
    signal?: AbortSignal
  ): Promise<CompleteAuthoritySnapshot | undefined> {
    const candidates = new Map<string, ZLinkAuthorityKey>();
    let cursor: Parameters<ZLinkAuthorityStore['listAuthorities']>[1];
    do {
      const page = await this.options.store.listAuthorities(
        '',
        cursor,
        this.options.pageSize,
        signal
      );
      if (page.kind === 'scanExpired') return undefined;
      for (const entry of page.items) {
        candidates.set(entry.key.value, entry.key);
      }
      cursor = page.nextCursor;
    } while (cursor !== undefined);

    const result = new Map<string, AppliedAuthorityRoute>();
    const pending: PendingInstanceActivationRecovery[] = [];
    for (const key of candidates.values()) {
      const current = await this.options.store.readAuthority(key, signal);
      if (current.kind !== 'snapshot') continue;
      const route = authorityRoute(current);
      if (route !== undefined) result.set(authorityRouteKey(route), route);
      const pendingRecovery = pendingInstanceActivation(current);
      if (pendingRecovery !== undefined) pending.push(pendingRecovery);
    }
    return { routes: result, pending };
  }
}

function pendingInstanceActivation(
  snapshot: ZLinkAuthoritySnapshot
): PendingInstanceActivationRecovery | undefined {
  const allocation = snapshot.allocation;
  const projection = snapshot.pendingCreation;
  const decoded = decodeServiceInstanceAuthorityPayload(snapshot.payload);
  if (
    allocation.state !== 'pending'
    || allocation.objectKind !== 'instance_spot'
    || projection === undefined
    || decoded?.state !== 'coldActivating'
    || decoded.stableType !== allocation.stableType
    || decoded.ownerId !== snapshot.ownerId
    || decoded.ownerLeaseGeneration !== snapshot.ownerLeaseGeneration
    || decoded.ownerMeshName !== allocation.descriptor.meshName
    || decoded.ownerNodeGeneration !== allocation.descriptorLifecycleGeneration
    || !routingIdsEqual(decoded.ownerNodeRid, allocation.descriptor.rid)
  ) {
    return undefined;
  }
  return {
    targetMeshName: allocation.descriptor.meshName,
    targetNodeRid: String(allocation.descriptor.rid),
    targetNodeGeneration: allocation.descriptorLifecycleGeneration,
    stableType: allocation.stableType,
    spotRid: decoded.spotRid,
    pending: {
      reservationId: projection.reservationId,
      storeVersion: snapshot.storeVersion.value,
      objectGeneration: snapshot.objectGeneration,
      authorityOwnerGeneration: snapshot.authorityOwnerGeneration,
      ownerId: snapshot.ownerId,
      ownerLeaseGeneration: snapshot.ownerLeaseGeneration,
      meshName: allocation.descriptor.meshName,
      nodeRid: String(allocation.descriptor.rid),
      nodeGeneration: allocation.descriptorLifecycleGeneration,
      requestReference: projection.requestContentReference,
      requestSha256: projection.requestSha256,
      requestEncodedSize: projection.requestEncodedSize
    }
  };
}

function authorityRoute(snapshot: ZLinkAuthoritySnapshot): AppliedAuthorityRoute | undefined {
  const allocation = snapshot.allocation;
  const decoded = decodeServiceReadySpotAuthority(snapshot.payload);
  if (
    allocation.state !== 'active'
    || allocation.objectKind === 'actor'
    || allocation.descriptor.meshName.length === 0
    || decoded === undefined
    || decoded.kind !== allocation.objectKind
    || decoded.stableType !== allocation.stableType
    || decoded.ownerId !== snapshot.ownerId
    || decoded.ownerLeaseGeneration !== snapshot.ownerLeaseGeneration
    || decoded.ownerMeshName !== allocation.descriptor.meshName
    || decoded.ownerNodeGeneration !== allocation.descriptorLifecycleGeneration
    || !routingIdsEqual(decoded.ownerNodeRid, allocation.descriptor.rid)
  ) {
    return undefined;
  }
  const spotRid = decoded.spotRid;
  const targetNodeRid = String(allocation.descriptor.rid);
  const spotRoute: ServiceSpotRouteFence = {
    spot: { spotRid, generation: snapshot.objectGeneration },
    targetNodeRid,
    targetNodeGeneration: allocation.descriptorLifecycleGeneration,
    authorityOwnerGeneration: snapshot.authorityOwnerGeneration
  };
  return {
    kind: allocation.objectKind,
    stableType: allocation.stableType,
    meshName: allocation.descriptor.meshName,
    spotRoute,
    instanceRoute: {
      targetNodeRid,
      targetNodeGeneration: allocation.descriptorLifecycleGeneration,
      targetSpotRid: spotRid,
      objectGeneration: snapshot.objectGeneration,
      ownerId: snapshot.ownerId,
      authorityOwnerGeneration: snapshot.authorityOwnerGeneration,
      leaseGeneration: snapshot.ownerLeaseGeneration,
      storeVersion: snapshot.storeVersion.value
    },
    ...(decoded.activationRecovery === undefined
      ? {}
      : { activationRecovery: decoded.activationRecovery })
  };
}

function authorityRouteKey(route: AppliedAuthorityRoute): string {
  return `${route.meshName}\0${route.spotRoute.spot.spotRid}\0`
    + `${route.spotRoute.spot.generation}`;
}

function sameAppliedRoute(
  left: AppliedAuthorityRoute,
  right: AppliedAuthorityRoute | undefined
): boolean {
  if (right === undefined) return false;
  return left.kind === right.kind
    && left.stableType === right.stableType
    && left.meshName === right.meshName
    && left.instanceRoute.targetNodeRid === right.instanceRoute.targetNodeRid
    && left.instanceRoute.targetNodeGeneration === right.instanceRoute.targetNodeGeneration
    && left.instanceRoute.authorityOwnerGeneration === right.instanceRoute.authorityOwnerGeneration
    && left.instanceRoute.storeVersion === right.instanceRoute.storeVersion;
}

function asStatefulAuthorityRouteSink(
  node: ZLinkBackendMeshNode
): StatefulAuthorityRouteSink | undefined {
  const candidate = node as ZLinkBackendMeshNode & Partial<StatefulAuthorityRouteSink>;
  return typeof candidate.rememberSpotRoute === 'function'
    && typeof candidate.forgetSpotRoute === 'function'
    && typeof candidate.registerInstanceIntent === 'function'
    && typeof candidate.forgetInstanceIntent === 'function'
    && typeof candidate.recoverInstanceActivation === 'function'
    && typeof candidate.completeRecoveredInstanceActivation === 'function'
    ? candidate as StatefulAuthorityRouteSink
    : undefined;
}

function aborted(signal: AbortSignal): boolean {
  return signal.aborted;
}

function wait(timeoutMs: number, signal: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(signal.reason);
      return;
    }
    const onAbort = () => {
      clearTimeout(timer);
      reject(signal.reason);
    };
    const timer = setTimeout(() => {
      signal.removeEventListener('abort', onAbort);
      resolve();
    }, timeoutMs);
    signal.addEventListener('abort', onAbort, { once: true });
  });
}
