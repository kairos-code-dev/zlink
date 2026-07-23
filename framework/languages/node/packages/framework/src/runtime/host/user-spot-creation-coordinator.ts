import { createHash } from 'node:crypto';
import type {
  RoutingId,
  SpotRef,
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStore,
  ZLinkLocationOwnerToken,
  ZLinkObjectCreationStore,
  ZLinkSpotCreateResult
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotCreateState
} from '../../contracts';
import {
  decodeServiceReadySpotAuthority,
  encodeServiceUserSpotAuthorityPayload
} from '../foundation/service-authority-payload-codec';
import { encodeAuthorityKey } from '../locations/authority-key-codec';
import type { ZLinkLocalSpotCreateResult } from '../spots/spot-manager-internal-contracts';

export interface ZLinkUserSpotCreationCoordinatorOptions {
  readonly store: ZLinkObjectCreationStore & ZLinkAuthorityStore;
  readonly target: (request: Pick<
    ZLinkUserSpotCreationRequest,
    'meshName' | 'stableType' | 'placementProfile' | 'affinityKey'
  >, signal?: AbortSignal) => Promise<{
    readonly meshName: string;
    readonly nodeRid: RoutingId;
    readonly nodeGeneration: bigint;
    readonly owner: ZLinkLocationOwnerToken;
    readonly isLocal: boolean;
  } | undefined>;
  readonly pollIntervalMs?: number;
  readonly cleanupTimeoutMs?: number;
}

export interface ZLinkUserSpotCreationRequest {
  readonly meshName?: string;
  readonly spotRid: RoutingId;
  readonly stableType: string;
  readonly placementProfile?: import('../../contracts').ZLinkPlacementProfile;
  readonly affinityKey?: import('../../contracts').ZLinkAffinityKey;
  readonly requestPayload: Uint8Array;
  readonly timeoutMs: number;
  readonly signal?: AbortSignal;
  readonly retryRidCollision?: boolean;
}

export interface ZLinkUserSpotCreationResult {
  readonly result: ZLinkSpotCreateResult;
  readonly spot: SpotRef;
}

/**
 * Internal control flow used only when create() generated the colliding RID.
 */
export class ZLinkUserSpotRidCollisionError extends Error {
  constructor(readonly spotRid: RoutingId) {
    super(`Generated User Spot RID '${String(spotRid)}' already exists.`);
    this.name = 'ZLinkUserSpotRidCollisionError';
  }
}

/**
 * Makes generic Location Store reservation the only visibility barrier for a
 * User Spot. Only the reservation winner invokes application lifecycle code.
 */
export class ZLinkUserSpotCreationCoordinator {
  constructor(private readonly options: ZLinkUserSpotCreationCoordinatorOptions) {}

  async getOrCreate(
    request: ZLinkUserSpotCreationRequest,
    materialize: (target: {
      readonly meshName: string;
      readonly nodeRid: RoutingId;
      readonly isLocal: boolean;
    }, signal: AbortSignal) => Promise<ZLinkLocalSpotCreateResult>,
    discard?: (signal?: AbortSignal) => Promise<void>
  ): Promise<ZLinkUserSpotCreationResult> {
    const deadline = createDeadline(request.timeoutMs, request.signal);
    const signal = deadline.signal;
    signal.throwIfAborted();
    let target;
    try {
      target = await this.options.target(request, signal);
    } catch (error) {
      deadline.close();
      if (error instanceof ZLinkFrameworkException) throw error;
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'User Spot placement provider failed.',
        false,
        error
      );
    }
    if (target === undefined) {
      deadline.close();
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
        'No eligible User Spot placement target is ready.',
        true
      );
    }
    if (!target.isLocal) {
      deadline.close();
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'Remote User Spot creation requires the generation-fenced internal create operation.'
      );
    }
    const owner = target.owner;
    const checksum = crc32c(request.requestPayload);
    const sha256 = createHash('sha256').update(request.requestPayload).digest();
    const contentReference = encodeLocationCreationContent(request.requestPayload, checksum);
    const authorityTarget = {
      meshName: target.meshName,
      nodeRid: target.nodeRid,
      nodeLifecycleGeneration: target.nodeGeneration,
      owner
    };
    let reserved;
    try {
      reserved = await this.options.store.reserve({
        key: { kind: 'user_spot', globalId: String(request.spotRid) },
        intent: {
          stableType: request.stableType,
          placementProfile: request.placementProfile,
          affinityKey: request.affinityKey,
          requestContentReference: contentReference,
          requestSha256: sha256,
          requestEncodedSize: BigInt(request.requestPayload.byteLength)
        },
        target: authorityTarget,
        creatingPayload: encodeServiceUserSpotAuthorityPayload({
          state: 'creating',
          stableType: request.stableType,
          spotRid: String(request.spotRid),
          ownerId: owner.ownerId,
          ownerLeaseGeneration: owner.leaseGeneration,
          ownerMeshName: target.meshName,
          ownerNodeRid: String(target.nodeRid),
          ownerNodeGeneration: target.nodeGeneration
        }),
        pendingCapacityDelta: 1
      }, signal);
    } catch (error) {
      deadline.close();
      if (error instanceof ZLinkFrameworkException) throw error;
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        'User Spot reservation Store operation failed.',
        false,
        error
      );
    }
    if (
      reserved.kind === 'alreadyExists'
      || (
        reserved.kind === 'conflict'
        && reserved.current.kind === 'snapshot'
        && reserved.current.allocation.objectKind === 'user_spot'
        && reserved.current.allocation.stableType === request.stableType
      )
    ) {
      const current = reserved.kind === 'alreadyExists'
        ? reserved.current
        : reserved.current;
      if (
        current.kind !== 'snapshot'
        || !sameCreationTarget(current, authorityTarget)
      ) {
        deadline.close();
        if (request.retryRidCollision === true) {
          throw new ZLinkUserSpotRidCollisionError(request.spotRid);
        }
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          'User Spot creation requires a remote generation-fenced create operation.'
        );
      }
      const spot = await this.awaitReady(request, signal);
      deadline.close();
      return {
        result: { spot, state: ZLinkSpotCreateState.Existing },
        spot
      };
    }
    if (reserved.kind === 'typeMismatch') {
      deadline.close();
      if (request.retryRidCollision === true) {
        throw new ZLinkUserSpotRidCollisionError(request.spotRid);
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotTypeMismatch,
        `User Spot '${String(request.spotRid)}' has another stable type.`
      );
    }
    if (reserved.kind !== 'reserved') {
      deadline.close();
      if (reserved.kind === 'placementCapacityExhausted') {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
          'User Spot placement capacity is exhausted.',
          true
        );
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestFailed,
        `User Spot reservation failed: ${reserved.kind}.`
      );
    }

    let result: ZLinkLocalSpotCreateResult;
    try {
      result = await materialize(target, signal);
      if (result.state === ZLinkSpotCreateState.Rejected) {
        const spot = spotRef(reserved.creating, request, true);
        await this.abort(
          request,
          reserved.reservationId,
          reserved.creating.storeVersion.value,
          authorityTarget,
          signal
        );
        await discard?.(signal);
        deadline.close();
        return { result: { spot, state: result.state, reply: result.reply }, spot };
      }
      let committed;
      try {
        committed = await this.options.store.commit({
          key: { kind: 'user_spot', globalId: String(request.spotRid) },
          reservationId: reserved.reservationId,
          expectedStoreVersion: reserved.creating.storeVersion.value,
          target: authorityTarget,
          readyPayload: encodeServiceUserSpotAuthorityPayload({
            state: 'ready',
            stableType: request.stableType,
            spotRid: String(request.spotRid),
            ownerId: owner.ownerId,
            ownerLeaseGeneration: owner.leaseGeneration,
            ownerMeshName: target.meshName,
            ownerNodeRid: String(target.nodeRid),
            ownerNodeGeneration: target.nodeGeneration
          })
        }, signal);
      } catch (error) {
        if (error instanceof ZLinkFrameworkException) throw error;
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          'User Spot Ready commit Store operation failed.',
          false,
          error
        );
      }
      if (committed.kind !== 'committed' && committed.kind !== 'alreadyCommitted') {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RequestFailed,
          `User Spot Ready commit failed: ${committed.kind}.`
        );
      }
      const spot = spotRef(committed.ready, request);
      deadline.close();
      return {
        result: { spot, state: result.state, reply: result.reply },
        spot
      };
    } catch (error) {
      const cleanupDeadline = createDeadline(this.options.cleanupTimeoutMs ?? 5_000);
      const cleanupSignal = cleanupDeadline.signal;
      let current;
      try {
        current = await this.options.store.readAuthority(
          encodeAuthorityKey('user_spot', String(request.spotRid)),
          cleanupSignal
        );
      } catch (reconcileError) {
        cleanupDeadline.close();
        deadline.close();
        throw new AggregateError(
          [error, reconcileError],
          'User Spot creation reconciliation failed.'
        );
      }
      if (current.kind === 'snapshot' && current.allocation.state === 'active') {
        const spot = spotRef(current, request);
        cleanupDeadline.close();
        deadline.close();
        return {
          result: { spot, state: ZLinkSpotCreateState.Existing },
          spot
        };
      }
      const cleanup = await settleCleanup([
        this.abort(
          request,
          reserved.reservationId,
          reserved.creating.storeVersion.value,
          authorityTarget,
          cleanupSignal
        ),
        discard?.(cleanupSignal) ?? Promise.resolve()
      ], cleanupSignal);
      cleanupDeadline.close();
      const cleanupErrors = cleanup
        .filter((item): item is PromiseRejectedResult => item.status === 'rejected')
        .map(item => item.reason);
      if (cleanupErrors.length > 0) {
        deadline.close();
        throw new AggregateError([error, ...cleanupErrors], 'User Spot creation rollback failed.');
      }
      deadline.close();
      throw error;
    }
  }

  async close(
    spot: SpotRef,
    closeOwner: (current: SpotRef) => Promise<boolean>,
    signal?: AbortSignal
  ): Promise<boolean> {
    const key = encodeAuthorityKey('user_spot', String(spot.spotRid));
    const current = await this.options.store.readAuthority(key, signal);
    if (current.kind === 'missing') return false;
    if (current.objectGeneration !== spot.objectGeneration) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotGenerationStale,
        `User Spot '${String(spot.spotRid)}' generation is stale.`
      );
    }
    const currentRef = spotRef(current, {
      meshName: current.allocation.descriptor.meshName,
      spotRid: spot.spotRid,
      stableType: current.allocation.stableType,
      requestPayload: Buffer.alloc(0),
      timeoutMs: 1
    });
    if (!await closeOwner(currentRef)) return false;
    const deleted = await this.options.store.compareExchangeAuthority(
      key,
      current.storeVersion,
      { kind: 'delete' },
      signal
    );
    if (deleted.kind === 'deleted') return true;
    if (deleted.kind === 'conflict') {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.SpotMoving,
        `User Spot '${String(spot.spotRid)}' authority changed while closing.`,
        true
      );
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.SpotGenerationStale,
      `User Spot '${String(spot.spotRid)}' generation cannot be closed.`
    );
  }

  private async awaitReady(
    request: ZLinkUserSpotCreationRequest,
    signal: AbortSignal
  ): Promise<SpotRef> {
    const key = encodeAuthorityKey('user_spot', String(request.spotRid));
    for (;;) {
      signal.throwIfAborted();
      const current = await this.options.store.readAuthority(key, signal);
      if (current.kind === 'snapshot' && current.allocation.state === 'active') {
        return spotRef(current, request);
      }
      await wait(this.options.pollIntervalMs ?? 10, signal);
    }
  }

  private async abort(
    request: ZLinkUserSpotCreationRequest,
    reservationId: string,
    expectedStoreVersion: string,
    target: Parameters<ZLinkObjectCreationStore['abort']>[0]['target'],
    signal?: AbortSignal
  ): Promise<void> {
    await this.options.store.abort({
      key: { kind: 'user_spot', globalId: String(request.spotRid) },
      reservationId,
      expectedStoreVersion,
      target
    }, signal);
  }
}

function spotRef(
  snapshot: ZLinkAuthoritySnapshot,
  request: ZLinkUserSpotCreationRequest,
  allowPending = false
): SpotRef {
  const decoded = decodeServiceReadySpotAuthority(snapshot.payload);
  if (
    (
      allowPending
        ? (
            snapshot.allocation.objectKind !== 'user_spot'
            || snapshot.allocation.stableType !== request.stableType
          )
        : (
            decoded?.kind !== 'user_spot'
            || decoded.stableType !== request.stableType
            || decoded.spotRid !== String(request.spotRid)
          )
    )
  ) {
    throw new Error('User Spot Ready authority does not match the requested identity.');
  }
  return {
    spotRid: request.spotRid,
    objectGeneration: snapshot.objectGeneration,
    meshName: snapshot.allocation.descriptor.meshName,
    nodeRid: snapshot.allocation.descriptor.rid
  };
}

function encodeLocationCreationContent(payload: Uint8Array, checksum: number): string {
  return `inline-v1:${checksum.toString(16).padStart(8, '0')}:${Buffer.from(payload).toString('base64url')}`;
}

function sameCreationTarget(
  snapshot: ZLinkAuthoritySnapshot,
  target: Parameters<ZLinkObjectCreationStore['reserve']>[0]['target']
): boolean {
  return snapshot.allocation.descriptor.meshName === target.meshName
    && String(snapshot.allocation.descriptor.rid) === String(target.nodeRid)
    && snapshot.allocation.descriptorLifecycleGeneration === target.nodeLifecycleGeneration
    && snapshot.ownerId === target.owner.ownerId
    && snapshot.ownerLeaseGeneration === target.owner.leaseGeneration;
}

function crc32c(payload: Uint8Array): number {
  let crc = 0xffff_ffff;
  for (const value of payload) {
    crc ^= value;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ (0x82f6_3b78 & -(crc & 1));
    }
  }
  return (~crc) >>> 0;
}

function createDeadline(timeoutMs: number, parent?: AbortSignal): {
  readonly signal: AbortSignal;
  close(): void;
} {
  const controller = new AbortController();
  const timeout = setTimeout(
    () => controller.abort(new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.DeadlineExceeded,
      'User Spot creation exceeded its end-to-end deadline.',
      true
    )),
    timeoutMs
  );
  const abort = () => controller.abort(parent?.reason);
  parent?.addEventListener('abort', abort, { once: true });
  return {
    signal: controller.signal,
    close: () => {
      clearTimeout(timeout);
      parent?.removeEventListener('abort', abort);
    }
  };
}

async function settleCleanup(
  operations: readonly Promise<unknown>[],
  signal: AbortSignal
): Promise<PromiseSettledResult<unknown>[]> {
  const settled = Promise.allSettled(operations);
  const aborted = new Promise<PromiseSettledResult<unknown>[]>(resolve => {
    const finish = () => resolve([{
      status: 'rejected',
      reason: signal.reason ?? new Error('User Spot cleanup deadline exceeded.')
    }]);
    if (signal.aborted) finish();
    else signal.addEventListener('abort', finish, { once: true });
  });
  const result = await Promise.race([settled, aborted]);
  void settled.catch(() => undefined);
  return result;
}

function wait(delayMs: number, signal?: AbortSignal): Promise<void> {
  return new Promise((resolve, reject) => {
    const done = () => {
      signal?.removeEventListener('abort', abort);
      resolve();
    };
    const timer = setTimeout(done, delayMs);
    const abort = () => {
      clearTimeout(timer);
      reject(signal?.reason);
    };
    signal?.addEventListener('abort', abort, { once: true });
  });
}
