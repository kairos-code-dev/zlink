import { randomUUID } from 'node:crypto';
import type {
  ZLinkAggregateAbortResult,
  ZLinkAggregateCommitResult,
  ZLinkAggregateFence,
  ZLinkAggregatePrepareRequest,
  ZLinkAggregatePrepareResult,
  ZLinkAuthorityCompareExchangeResult,
  ZLinkAuthorityKey,
  ZLinkAuthorityMutation,
  ZLinkAuthorityReadResult,
  ZLinkAuthorityScanCursor,
  ZLinkAuthorityScanResult,
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStoreVersion,
  ZLinkLocationOwnerToken,
  ZLinkMeshNodeDescriptorKey,
  ZLinkObjectAbortRequest,
  ZLinkObjectAbortResult,
  ZLinkObjectCommitRequest,
  ZLinkObjectCommitResult,
  ZLinkObjectReserveRequest,
  ZLinkObjectReserveResult,
  ZLinkPlacementAllocation,
  ZLinkRelocationCapacityAbortResult,
  ZLinkRelocationCapacityFence,
  ZLinkRelocationCapacityReservationRequest,
  ZLinkRelocationCapacityReserveResult
} from '../../contracts/Locations';

const MAX_GENERATION = 0x7fff_ffff_ffff_ffffn;
const MAX_PAYLOAD_BYTES = 1024 * 1024;

export interface ZLinkInMemoryAuthorityValidation {
  isTargetLive(
    descriptor: ZLinkMeshNodeDescriptorKey,
    lifecycleGeneration: bigint,
    owner: ZLinkLocationOwnerToken
  ): boolean;
  placementCapacityAvailable?(
    descriptor: ZLinkMeshNodeDescriptorKey,
    objectKind: ZLinkPlacementAllocation['objectKind'],
    stableType: string,
    requestedDelta: number,
    currentPending: number,
    currentActive: number,
    placementProfile?: string
  ): boolean;
}

interface AuthorityRow {
  snapshot: StoredSnapshot;
  creation?: {
    readonly reservationId: string;
    readonly target: CreationTarget;
    terminal?: 'committed' | 'aborted';
  };
}

type StoredSnapshot = Omit<ZLinkAuthoritySnapshot, 'kind' | 'storeNow'>;

interface CreationTarget {
  readonly descriptor: ZLinkMeshNodeDescriptorKey;
  readonly lifecycleGeneration: bigint;
  readonly owner: ZLinkLocationOwnerToken;
}

interface CapacityReservation {
  readonly request: ZLinkRelocationCapacityReservationRequest;
  readonly fence: ZLinkRelocationCapacityFence;
  state: 'reserved' | 'prepared' | 'committed' | 'aborted';
  aggregate?: string;
}

interface AggregateRecord {
  readonly request: ZLinkAggregatePrepareRequest;
  readonly fence: ZLinkAggregateFence;
  state: 'prepared' | 'committed' | 'aborted';
}

/**
 * In-memory reference provider for the provider-owned authority state machine.
 * Opaque payload bytes are copied but never parsed.
 */
export class ZLinkInMemoryAuthorityStore {
  private readonly rows = new Map<string, AuthorityRow>();
  private readonly creationTerminals = new Map<string, 'committed' | 'aborted'>();
  private readonly capacityReservations = new Map<string, CapacityReservation>();
  private readonly aggregates = new Map<string, AggregateRecord>();
  private readonly activeCapacity = new Map<string, number>();
  private readonly pendingCapacity = new Map<string, number>();
  private storeVersion = 0n;
  private objectGeneration = 0n;
  private ownerGeneration = 0n;
  private scanRevision = 0n;

  constructor(
    private readonly validation: ZLinkInMemoryAuthorityValidation,
    private readonly now: () => Date = () => new Date()
  ) {}

  async readAuthority(
    key: ZLinkAuthorityKey,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityReadResult> {
    signal?.throwIfAborted();
    const value = requireText(key.value, 'authority key');
    return this.read(value);
  }

  async compareExchangeAuthority(
    key: ZLinkAuthorityKey,
    expectedStoreVersion: ZLinkAuthorityStoreVersion,
    mutation: ZLinkAuthorityMutation,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityCompareExchangeResult> {
    signal?.throwIfAborted();
    const keyValue = requireText(key.value, 'authority key');
    validateAuthorityMutation(mutation);
    const row = this.rows.get(keyValue);
    if (
      row === undefined
      || row.snapshot.allocation.state !== 'active'
      || row.snapshot.storeVersion.value !== expectedStoreVersion.value
    ) {
      return { kind: 'conflict', current: this.read(keyValue) };
    }

    if (mutation.kind === 'delete') {
      if (!this.isOwnerLive(row.snapshot)) {
        return { kind: 'conflict', current: this.read(keyValue) };
      }
      const nextVersion = this.tryNextStoreVersion();
      if (nextVersion === undefined) return { kind: 'generationExhausted' };
      this.adjust(this.activeCapacity, allocationCapacityKey(row.snapshot.allocation),
        -row.snapshot.allocation.capacityDelta);
      this.rows.delete(keyValue);
      this.scanRevision++;
      return { kind: 'deleted', storeVersion: version(nextVersion), storeNow: this.now() };
    }

    validatePayload(mutation.payload);
    if (mutation.generationTransition === 'preserve') {
      if (!this.isOwnerLive(row.snapshot)) {
        return { kind: 'conflict', current: this.read(keyValue) };
      }
      const nextVersion = this.tryNextStoreVersion();
      if (nextVersion === undefined) return { kind: 'generationExhausted' };
      row.snapshot = {
        ...row.snapshot,
        storeVersion: version(nextVersion),
        payload: Buffer.from(mutation.payload)
      };
      this.scanRevision++;
      return this.stored(row.snapshot);
    }

    const targetOwner = mutation.targetOwner!;
    const reservation = this.capacityReservations.get(mutation.relocationCapacityFence!.value);
    if (
      reservation === undefined
      || reservation.state !== 'reserved'
      || !sameFenceAuthority(reservation.request, keyValue, expectedStoreVersion, row.snapshot)
      || !sameOwner(reservation.request.targetOwner, targetOwner)
      || !this.targetLive(reservation.request)
    ) {
      return { kind: 'conflict', current: this.read(keyValue) };
    }
    if (this.storeVersion >= MAX_GENERATION || this.ownerGeneration >= MAX_GENERATION) {
      return { kind: 'generationExhausted' };
    }
    const nextVersion = ++this.storeVersion;
    const nextOwnerGeneration = ++this.ownerGeneration;
    this.moveCapacity(row.snapshot.allocation, reservation.request);
    row.snapshot = {
      storeVersion: version(nextVersion),
      payload: Buffer.from(mutation.payload),
      objectGeneration: row.snapshot.objectGeneration,
      authorityOwnerGeneration: nextOwnerGeneration,
      ownerId: targetOwner.ownerId,
      ownerLeaseGeneration: targetOwner.leaseGeneration,
      allocation: targetAllocation(reservation.request)
    };
    reservation.state = 'committed';
    this.scanRevision++;
    return this.stored(row.snapshot);
  }

  async listAuthorities(
    prefix: string,
    cursor: ZLinkAuthorityScanCursor | undefined,
    limit: number,
    signal?: AbortSignal
  ): Promise<ZLinkAuthorityScanResult> {
    signal?.throwIfAborted();
    if (!Number.isInteger(limit) || limit < 1 || limit > 1000) {
      throw new RangeError('Authority scan limit must be in 1..1000.');
    }
    const decoded = cursor === undefined
      ? { revision: this.scanRevision, offset: 0, prefix }
      : decodeCursor(cursor.encoded);
    if (decoded.revision !== this.scanRevision || decoded.prefix !== prefix) {
      return { kind: 'scanExpired' };
    }
    const rows = [...this.rows.entries()]
      .filter(([key]) => key.startsWith(prefix))
      .sort(([left], [right]) => left.localeCompare(right));
    const selected = rows.slice(decoded.offset, decoded.offset + limit);
    const nextOffset = decoded.offset + selected.length;
    return {
      kind: 'page',
      items: selected.map(([key, row]) => ({
        key: authorityKey(key),
        snapshot: this.snapshot(row.snapshot)
      })),
      nextCursor: nextOffset < rows.length
        ? scanCursor(this.scanRevision, nextOffset, prefix)
        : undefined
    };
  }

  async reserve(
    request: ZLinkObjectReserveRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectReserveResult> {
    signal?.throwIfAborted();
    validateReserve(request);
    const key = creationKey(request.key);
    const current = this.rows.get(key);
    if (current !== undefined) {
      if (
        current.snapshot.allocation.objectKind !== request.key.kind
        || current.snapshot.allocation.stableType !== request.intent.stableType
      ) {
        return { kind: 'typeMismatch', current: this.snapshot(current.snapshot) };
      }
      if (current.snapshot.allocation.state === 'active') {
        return { kind: 'alreadyExists', current: this.snapshot(current.snapshot) };
      }
      return { kind: 'conflict', current: this.read(key) };
    }
    const target = creationTarget(request);
    if (!this.validation.isTargetLive(target.descriptor, target.lifecycleGeneration, target.owner)) {
      return { kind: 'conflict', current: this.read(key) };
    }
    const allocation: ZLinkPlacementAllocation = {
      state: 'pending',
      objectKind: request.key.kind,
      stableType: request.intent.stableType,
      descriptor: target.descriptor,
      descriptorLifecycleGeneration: target.lifecycleGeneration,
      capacityDelta: request.pendingCapacityDelta
    };
    if (!this.hasPendingCapacity(allocation, request.intent.placementProfile)) {
      return { kind: 'placementCapacityExhausted' };
    }
    if (
      this.storeVersion >= MAX_GENERATION
      || this.objectGeneration >= MAX_GENERATION
      || this.ownerGeneration >= MAX_GENERATION
    ) {
      return { kind: 'generationExhausted' };
    }
    const reservationId = randomUUID();
    const snapshot: StoredSnapshot = {
      storeVersion: version(++this.storeVersion),
      payload: Buffer.from(request.creatingPayload),
      objectGeneration: ++this.objectGeneration,
      authorityOwnerGeneration: ++this.ownerGeneration,
      ownerId: target.owner.ownerId,
      ownerLeaseGeneration: target.owner.leaseGeneration,
      allocation
    };
    this.rows.set(key, { snapshot, creation: { reservationId, target } });
    this.adjust(this.pendingCapacity, allocationCapacityKey(allocation), allocation.capacityDelta);
    this.scanRevision++;
    return { kind: 'reserved', reservationId, creating: this.snapshot(snapshot) };
  }

  async commit(
    request: ZLinkObjectCommitRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectCommitResult> {
    signal?.throwIfAborted();
    validatePayload(request.readyPayload);
    const terminal = this.creationTerminals.get(request.reservationId);
    const key = creationKey(request.key);
    const row = this.rows.get(key);
    if (terminal === 'committed' && row?.snapshot.allocation.state === 'active') {
      return { kind: 'alreadyCommitted', ready: this.snapshot(row.snapshot) };
    }
    if (
      row === undefined
      || row.snapshot.allocation.state !== 'pending'
      || row.creation?.reservationId !== request.reservationId
      || row.snapshot.storeVersion.value !== request.expectedStoreVersion
      || !sameCreationTarget(row.creation.target, request.target)
      || !this.validation.isTargetLive(
        row.creation.target.descriptor,
        row.creation.target.lifecycleGeneration,
        row.creation.target.owner
      )
    ) {
      return { kind: 'stale' };
    }
    const nextVersion = this.tryNextStoreVersion();
    if (nextVersion === undefined) return { kind: 'generationExhausted' };
    const pending = row.snapshot.allocation;
    this.adjust(this.pendingCapacity, allocationCapacityKey(pending), -pending.capacityDelta);
    const active = { ...pending, state: 'active' as const };
    this.adjust(this.activeCapacity, allocationCapacityKey(active), active.capacityDelta);
    row.snapshot = {
      ...row.snapshot,
      storeVersion: version(nextVersion),
      payload: Buffer.from(request.readyPayload),
      allocation: active
    };
    row.creation.terminal = 'committed';
    this.creationTerminals.set(request.reservationId, 'committed');
    this.scanRevision++;
    return { kind: 'committed', ready: this.snapshot(row.snapshot) };
  }

  async abort(
    request: ZLinkObjectAbortRequest,
    signal?: AbortSignal
  ): Promise<ZLinkObjectAbortResult> {
    signal?.throwIfAborted();
    const terminal = this.creationTerminals.get(request.reservationId);
    if (terminal === 'aborted') return { kind: 'alreadyAborted' };
    const key = creationKey(request.key);
    const row = this.rows.get(key);
    if (
      row === undefined
      || row.snapshot.allocation.state !== 'pending'
      || row.creation?.reservationId !== request.reservationId
      || row.snapshot.storeVersion.value !== request.expectedStoreVersion
      || !sameCreationTarget(row.creation.target, request.target)
    ) {
      return { kind: 'stale' };
    }
    this.adjust(this.pendingCapacity, allocationCapacityKey(row.snapshot.allocation),
      -row.snapshot.allocation.capacityDelta);
    this.rows.delete(key);
    this.creationTerminals.set(request.reservationId, 'aborted');
    this.scanRevision++;
    return { kind: 'aborted' };
  }

  async reserveRelocationCapacity(
    request: ZLinkRelocationCapacityReservationRequest,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityReserveResult> {
    signal?.throwIfAborted();
    validateRelocationReservation(request);
    const existing = this.capacityReservations.get(request.reservationId);
    if (existing !== undefined) {
      return sameRelocationRequest(existing.request, request)
        ? { kind: 'alreadyReserved', fence: existing.fence }
        : { kind: 'conflict', current: this.read(request.authorityKey.value) };
    }
    const row = this.rows.get(request.authorityKey.value);
    if (
      row === undefined
      || row.snapshot.allocation.state !== 'active'
      || !sameFenceAuthority(request, request.authorityKey.value, request.expectedStoreVersion, row.snapshot)
    ) {
      return { kind: 'conflict', current: this.read(request.authorityKey.value) };
    }
    if (!this.targetLive(request)) return { kind: 'targetUnavailable' };
    const target = targetAllocation(request);
    if (!this.hasPendingCapacity(target)) return { kind: 'placementCapacityExhausted' };
    const fence = capacityFence(request.reservationId);
    this.capacityReservations.set(request.reservationId, {
      request: cloneRelocationRequest(request),
      fence,
      state: 'reserved'
    });
    this.adjust(this.pendingCapacity, allocationCapacityKey(target), request.capacityDelta);
    return { kind: 'reserved', fence };
  }

  async abortRelocationCapacity(
    fence: ZLinkRelocationCapacityFence,
    signal?: AbortSignal
  ): Promise<ZLinkRelocationCapacityAbortResult> {
    signal?.throwIfAborted();
    const reservation = this.capacityReservations.get(fence.value);
    if (reservation === undefined) return 'stale';
    if (reservation.state === 'aborted') return 'alreadyAborted';
    if (reservation.state === 'committed') return 'alreadyCommitted';
    if (reservation.state === 'prepared') return 'stale';
    reservation.state = 'aborted';
    this.adjust(this.pendingCapacity, relocationTargetCapacityKey(reservation.request),
      -reservation.request.capacityDelta);
    return 'aborted';
  }

  async prepareAggregate(
    request: ZLinkAggregatePrepareRequest,
    signal?: AbortSignal
  ): Promise<ZLinkAggregatePrepareResult> {
    signal?.throwIfAborted();
    validateAggregateRequest(request);
    const aggregateKey = aggregateRecordKey(request.aggregateId.value, request.aggregateGeneration);
    const existing = this.aggregates.get(aggregateKey);
    if (existing !== undefined) {
      if (existing.state === 'prepared' && sameAggregateRequest(existing.request, request)) {
        return { kind: 'alreadyPrepared', fence: existing.fence };
      }
      return existing.state === 'committed'
        ? { kind: 'stale' }
        : { kind: 'conflict' };
    }
    const reservationByAuthority = new Map<string, CapacityReservation>();
    for (const fence of request.targetReservations) {
      const reservation = this.capacityReservations.get(fence.value);
      if (reservation === undefined || reservation.state !== 'reserved') {
        return { kind: 'conflict' };
      }
      reservationByAuthority.set(reservation.request.authorityKey.value, reservation);
    }
    const newOwnerParticipants = request.participants.filter(
      participant => participant.ownerTransition === 'newOwner'
    );
    if (
      reservationByAuthority.size !== request.targetReservations.length
      || newOwnerParticipants.length !== request.targetReservations.length
    ) {
      return { kind: 'conflict' };
    }
    for (const participant of request.participants) {
      const row = this.rows.get(participant.authorityKey.value);
      if (
        row === undefined
        || row.snapshot.allocation.state !== 'active'
        || row.snapshot.storeVersion.value !== participant.expectedStoreVersion.value
      ) {
        return { kind: 'conflict' };
      }
      if (participant.ownerTransition === 'preserve') {
        if (!this.isOwnerLive(row.snapshot)) return { kind: 'conflict' };
        continue;
      }
      const reservation = reservationByAuthority.get(participant.authorityKey.value);
      if (
        reservation === undefined
        || !sameOwner(reservation.request.targetOwner, request.targetOwner)
        || !sameFenceAuthority(
          reservation.request,
          participant.authorityKey.value,
          participant.expectedStoreVersion,
          row.snapshot
        )
        || !this.targetLive(reservation.request)
      ) {
        return { kind: 'conflict' };
      }
    }
    const fence: ZLinkAggregateFence = {
      aggregateId: request.aggregateId,
      aggregateGeneration: request.aggregateGeneration
    };
    for (const reservation of reservationByAuthority.values()) {
      reservation.state = 'prepared';
      reservation.aggregate = aggregateKey;
    }
    this.aggregates.set(aggregateKey, {
      request: cloneAggregateRequest(request),
      fence,
      state: 'prepared'
    });
    return { kind: 'prepared', fence };
  }

  async commitAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateCommitResult> {
    signal?.throwIfAborted();
    const key = aggregateRecordKey(fence.aggregateId.value, fence.aggregateGeneration);
    const aggregate = this.aggregates.get(key);
    if (aggregate === undefined) return { kind: 'stale' };
    if (aggregate.state === 'committed') return { kind: 'alreadyCommitted' };
    if (aggregate.state !== 'prepared') return { kind: 'stale' };
    let versionsNeeded = 0n;
    let ownersNeeded = 0n;
    const rows: Array<{
      readonly participant: ZLinkAggregatePrepareRequest['participants'][number];
      readonly row: AuthorityRow;
      readonly reservation?: CapacityReservation;
    }> = [];
    for (const participant of aggregate.request.participants) {
      const row = this.rows.get(participant.authorityKey.value);
      if (
        row === undefined
        || row.snapshot.allocation.state !== 'active'
        || row.snapshot.storeVersion.value !== participant.expectedStoreVersion.value
      ) {
        return { kind: 'stale' };
      }
      let reservation: CapacityReservation | undefined;
      if (participant.ownerTransition === 'newOwner') {
        reservation = [...this.capacityReservations.values()].find(candidate =>
          candidate.aggregate === key
          && candidate.request.authorityKey.value === participant.authorityKey.value);
        if (
          reservation === undefined
          || reservation.state !== 'prepared'
          || !sameFenceAuthority(
            reservation.request,
            participant.authorityKey.value,
            participant.expectedStoreVersion,
            row.snapshot
          )
          || !this.targetLive(reservation.request)
        ) {
          return { kind: 'stale' };
        }
        ownersNeeded++;
      } else if (!this.isOwnerLive(row.snapshot)) {
        return { kind: 'stale' };
      }
      versionsNeeded++;
      rows.push({ participant, row, reservation });
    }
    if (
      this.storeVersion + versionsNeeded > MAX_GENERATION
      || this.ownerGeneration + ownersNeeded > MAX_GENERATION
    ) {
      return { kind: 'generationExhausted' };
    }
    for (const entry of rows) {
      const nextVersion = ++this.storeVersion;
      if (entry.reservation === undefined) {
        entry.row.snapshot = {
          ...entry.row.snapshot,
          storeVersion: version(nextVersion),
          payload: Buffer.from(entry.participant.authorityPayload)
        };
      } else {
        const owner = entry.reservation.request.targetOwner;
        this.moveCapacity(entry.row.snapshot.allocation, entry.reservation.request);
        entry.row.snapshot = {
          storeVersion: version(nextVersion),
          payload: Buffer.from(entry.participant.authorityPayload),
          objectGeneration: entry.row.snapshot.objectGeneration,
          authorityOwnerGeneration: ++this.ownerGeneration,
          ownerId: owner.ownerId,
          ownerLeaseGeneration: owner.leaseGeneration,
          allocation: targetAllocation(entry.reservation.request)
        };
        entry.reservation.state = 'committed';
      }
    }
    aggregate.state = 'committed';
    this.scanRevision++;
    return { kind: 'committed' };
  }

  async abortAggregate(
    fence: ZLinkAggregateFence,
    signal?: AbortSignal
  ): Promise<ZLinkAggregateAbortResult> {
    signal?.throwIfAborted();
    const key = aggregateRecordKey(fence.aggregateId.value, fence.aggregateGeneration);
    const aggregate = this.aggregates.get(key);
    if (aggregate === undefined) return { kind: 'stale' };
    if (aggregate.state === 'aborted') return { kind: 'alreadyAborted' };
    if (aggregate.state !== 'prepared') return { kind: 'stale' };
    for (const reservation of this.capacityReservations.values()) {
      if (reservation.aggregate !== key || reservation.state !== 'prepared') continue;
      reservation.state = 'aborted';
      this.adjust(this.pendingCapacity, relocationTargetCapacityKey(reservation.request),
        -reservation.request.capacityDelta);
    }
    aggregate.state = 'aborted';
    return { kind: 'aborted' };
  }

  private read(key: string): ZLinkAuthorityReadResult {
    const row = this.rows.get(key);
    return row === undefined
      ? { kind: 'missing', storeNow: this.now() }
      : this.snapshot(row.snapshot);
  }

  private snapshot(snapshot: StoredSnapshot): ZLinkAuthoritySnapshot {
    return {
      kind: 'snapshot',
      ...snapshot,
      payload: Buffer.from(snapshot.payload),
      allocation: cloneAllocation(snapshot.allocation),
      storeNow: this.now()
    };
  }

  private stored(snapshot: StoredSnapshot): ZLinkAuthorityCompareExchangeResult {
    const value = this.snapshot(snapshot);
    const { kind: _, ...stored } = value;
    return { kind: 'stored', ...stored };
  }

  private isOwnerLive(snapshot: StoredSnapshot): boolean {
    return this.validation.isTargetLive(
      snapshot.allocation.descriptor,
      snapshot.allocation.descriptorLifecycleGeneration,
      { ownerId: snapshot.ownerId, leaseGeneration: snapshot.ownerLeaseGeneration }
    );
  }

  private targetLive(request: ZLinkRelocationCapacityReservationRequest): boolean {
    return this.validation.isTargetLive(
      request.targetDescriptor,
      request.targetNodeLifecycleGeneration,
      request.targetOwner
    );
  }

  private hasPendingCapacity(allocation: ZLinkPlacementAllocation, placementProfile?: string): boolean {
    const key = allocationCapacityKey(allocation);
    const currentPending = this.pendingCapacity.get(key) ?? 0;
    const currentActive = this.activeCapacity.get(key) ?? 0;
    return this.validation.placementCapacityAvailable?.(
      allocation.descriptor,
      allocation.objectKind,
      allocation.stableType,
      allocation.capacityDelta,
      currentPending,
      currentActive,
      placementProfile
    ) ?? true;
  }

  private moveCapacity(
    source: ZLinkPlacementAllocation,
    request: ZLinkRelocationCapacityReservationRequest
  ): void {
    this.adjust(this.activeCapacity, allocationCapacityKey(source), -source.capacityDelta);
    this.adjust(this.pendingCapacity, relocationTargetCapacityKey(request), -request.capacityDelta);
    this.adjust(this.activeCapacity, relocationTargetCapacityKey(request), request.capacityDelta);
  }

  private adjust(values: Map<string, number>, key: string, delta: number): void {
    const next = (values.get(key) ?? 0) + delta;
    if (next < 0) throw new Error('In-memory placement capacity counter underflow.');
    if (next === 0) values.delete(key);
    else values.set(key, next);
  }

  private tryNextStoreVersion(): bigint | undefined {
    return this.storeVersion >= MAX_GENERATION ? undefined : ++this.storeVersion;
  }
}

function validateAuthorityMutation(mutation: ZLinkAuthorityMutation): void {
  if (mutation.kind === 'delete') return;
  const hasOwner = mutation.targetOwner !== undefined;
  const hasFence = mutation.relocationCapacityFence !== undefined;
  if (mutation.generationTransition === 'preserve' ? (hasOwner || hasFence) : (!hasOwner || !hasFence)) {
    throw new TypeError('Authority owner and relocation fence do not match the generation transition.');
  }
}

function validateReserve(request: ZLinkObjectReserveRequest): void {
  requireText(request.key.globalId, 'object global ID');
  requireText(request.intent.stableType, 'stable type');
  validatePayload(request.creatingPayload);
  validateCapacityDelta(request.pendingCapacityDelta);
  if (request.intent.requestSha256.byteLength !== 32 || request.intent.requestEncodedSize < 0n) {
    throw new TypeError('Object creation content receipt is invalid.');
  }
}

function validateRelocationReservation(request: ZLinkRelocationCapacityReservationRequest): void {
  requireText(request.reservationId, 'relocation reservation ID');
  requireText(request.stableType, 'stable type');
  validateCapacityDelta(request.capacityDelta);
}

function validateAggregateRequest(request: ZLinkAggregatePrepareRequest): void {
  requireText(request.aggregateId.value, 'aggregate ID');
  if (request.aggregateGeneration < 1n || request.participants.length < 1 || request.participants.length > 1024) {
    throw new RangeError('Aggregate generation and participant count are invalid.');
  }
  if (request.inventoryDigest.byteLength !== 32) {
    throw new TypeError('Aggregate inventory digest must contain 32 bytes.');
  }
  const keys = request.participants.map(participant => participant.authorityKey.value);
  const sorted = [...keys].sort();
  if (new Set(keys).size !== keys.length || keys.some((key, index) => key !== sorted[index])) {
    throw new TypeError('Aggregate participants must be unique and canonically sorted.');
  }
}

function validatePayload(payload: Uint8Array): void {
  if (payload.byteLength > MAX_PAYLOAD_BYTES) {
    throw new RangeError('Authority payload exceeds 1 MiB.');
  }
}

function validateCapacityDelta(value: number): void {
  if (!Number.isInteger(value) || value < 1 || value > 0x7fff_ffff) {
    throw new RangeError('Placement capacity delta must be in 1..2147483647.');
  }
}

function sameFenceAuthority(
  request: ZLinkRelocationCapacityReservationRequest,
  key: string,
  expectedVersion: ZLinkAuthorityStoreVersion,
  current: StoredSnapshot
): boolean {
  return request.authorityKey.value === key
    && request.expectedStoreVersion.value === expectedVersion.value
    && current.storeVersion.value === expectedVersion.value
    && current.allocation.state === 'active'
    && current.allocation.objectKind === request.objectKind
    && current.allocation.stableType === request.stableType
    && sameDescriptor(current.allocation.descriptor, request.sourceDescriptor)
    && current.allocation.descriptorLifecycleGeneration === request.sourceNodeLifecycleGeneration
    && current.allocation.capacityDelta === request.capacityDelta
    && current.ownerId === request.sourceOwner.ownerId
    && current.ownerLeaseGeneration === request.sourceOwner.leaseGeneration;
}

function targetAllocation(
  request: ZLinkRelocationCapacityReservationRequest
): ZLinkPlacementAllocation {
  return {
    state: 'active',
    objectKind: request.objectKind,
    stableType: request.stableType,
    descriptor: { ...request.targetDescriptor },
    descriptorLifecycleGeneration: request.targetNodeLifecycleGeneration,
    capacityDelta: request.capacityDelta
  };
}

function cloneAllocation(allocation: ZLinkPlacementAllocation): ZLinkPlacementAllocation {
  return { ...allocation, descriptor: { ...allocation.descriptor } };
}

function creationTarget(request: ZLinkObjectReserveRequest): CreationTarget {
  return {
    descriptor: { meshName: request.target.meshName, rid: request.target.nodeRid },
    lifecycleGeneration: request.target.nodeLifecycleGeneration,
    owner: { ...request.target.owner }
  };
}

function sameCreationTarget(target: CreationTarget, actual: ZLinkObjectCommitRequest['target']): boolean {
  return target.descriptor.meshName === actual.meshName
    && sameRid(target.descriptor.rid, actual.nodeRid)
    && target.lifecycleGeneration === actual.nodeLifecycleGeneration
    && sameOwner(target.owner, actual.owner);
}

function sameOwner(left: ZLinkLocationOwnerToken, right: ZLinkLocationOwnerToken): boolean {
  return left.ownerId === right.ownerId && left.leaseGeneration === right.leaseGeneration;
}

function sameDescriptor(left: ZLinkMeshNodeDescriptorKey, right: ZLinkMeshNodeDescriptorKey): boolean {
  return left.meshName === right.meshName && sameRid(left.rid, right.rid);
}

function sameRid(left: unknown, right: unknown): boolean {
  return String(left) === String(right);
}

function allocationCapacityKey(allocation: ZLinkPlacementAllocation): string {
  return capacityKey(
    allocation.descriptor,
    allocation.descriptorLifecycleGeneration,
    allocation.objectKind,
    allocation.stableType
  );
}

function relocationTargetCapacityKey(request: ZLinkRelocationCapacityReservationRequest): string {
  return capacityKey(
    request.targetDescriptor,
    request.targetNodeLifecycleGeneration,
    request.objectKind,
    request.stableType
  );
}

function capacityKey(
  descriptor: ZLinkMeshNodeDescriptorKey,
  lifecycle: bigint,
  kind: string,
  stableType: string
): string {
  return `${descriptor.meshName}\0${String(descriptor.rid)}\0${lifecycle}\0${kind}\0${stableType}`;
}

function creationKey(key: ZLinkObjectReserveRequest['key']): string {
  return `${key.kind}:${key.globalId}`;
}

function authorityKey(value: string): ZLinkAuthorityKey {
  return { value } as ZLinkAuthorityKey;
}

function version(value: bigint): ZLinkAuthorityStoreVersion {
  return { value: value.toString() } as ZLinkAuthorityStoreVersion;
}

function capacityFence(value: string): ZLinkRelocationCapacityFence {
  return { value } as ZLinkRelocationCapacityFence;
}

function scanCursor(revision: bigint, offset: number, prefix: string): ZLinkAuthorityScanCursor {
  const encoded = Buffer.from(JSON.stringify({
    revision: revision.toString(),
    offset,
    prefix
  })).toString('base64url');
  const factory = requireScanCursorFactory();
  return factory(encoded);
}

function requireScanCursorFactory(): (encoded: string) => ZLinkAuthorityScanCursor {
  // Imported as a type to keep the provider contract free of runtime cycles.
  return (encoded) => ({ encoded } as ZLinkAuthorityScanCursor);
}

function decodeCursor(encoded: string): { revision: bigint; offset: number; prefix: string } {
  try {
    const value = JSON.parse(Buffer.from(encoded, 'base64url').toString('utf8')) as {
      readonly revision: string;
      readonly offset: number;
      readonly prefix: string;
    };
    if (!Number.isInteger(value.offset) || value.offset < 0 || typeof value.prefix !== 'string') {
      throw new Error();
    }
    return { revision: BigInt(value.revision), offset: value.offset, prefix: value.prefix };
  } catch {
    throw new TypeError('Authority scan cursor is invalid.');
  }
}

function sameRelocationRequest(
  left: ZLinkRelocationCapacityReservationRequest,
  right: ZLinkRelocationCapacityReservationRequest
): boolean {
  return JSON.stringify(relocationComparable(left)) === JSON.stringify(relocationComparable(right));
}

function relocationComparable(request: ZLinkRelocationCapacityReservationRequest): unknown {
  return {
    ...request,
    expectedStoreVersion: request.expectedStoreVersion.value,
    sourceNodeLifecycleGeneration: request.sourceNodeLifecycleGeneration.toString(),
    targetNodeLifecycleGeneration: request.targetNodeLifecycleGeneration.toString(),
    sourceOwner: { ...request.sourceOwner, leaseGeneration: request.sourceOwner.leaseGeneration.toString() },
    targetOwner: { ...request.targetOwner, leaseGeneration: request.targetOwner.leaseGeneration.toString() },
    sourceDescriptor: {
      meshName: request.sourceDescriptor.meshName,
      rid: String(request.sourceDescriptor.rid)
    },
    targetDescriptor: {
      meshName: request.targetDescriptor.meshName,
      rid: String(request.targetDescriptor.rid)
    }
  };
}

function cloneRelocationRequest(
  request: ZLinkRelocationCapacityReservationRequest
): ZLinkRelocationCapacityReservationRequest {
  return {
    ...request,
    authorityKey: { ...request.authorityKey },
    expectedStoreVersion: { ...request.expectedStoreVersion },
    sourceOwner: { ...request.sourceOwner },
    targetOwner: { ...request.targetOwner },
    sourceDescriptor: { ...request.sourceDescriptor },
    targetDescriptor: { ...request.targetDescriptor }
  };
}

function aggregateRecordKey(id: string, generation: bigint): string {
  return `${id}\0${generation}`;
}

function sameAggregateRequest(
  left: ZLinkAggregatePrepareRequest,
  right: ZLinkAggregatePrepareRequest
): boolean {
  if (
    left.aggregateId.value !== right.aggregateId.value
    || left.aggregateGeneration !== right.aggregateGeneration
    || !sameOwner(left.targetOwner, right.targetOwner)
    || !Buffer.from(left.inventoryDigest).equals(Buffer.from(right.inventoryDigest))
    || left.participants.length !== right.participants.length
    || left.targetReservations.length !== right.targetReservations.length
  ) {
    return false;
  }
  return left.participants.every((participant, index) => {
    const other = right.participants[index]!;
    return participant.authorityKey.value === other.authorityKey.value
      && participant.expectedStoreVersion.value === other.expectedStoreVersion.value
      && participant.ownerTransition === other.ownerTransition
      && Buffer.from(participant.authorityPayload).equals(Buffer.from(other.authorityPayload))
      && Buffer.from(participant.membershipMutation).equals(Buffer.from(other.membershipMutation));
  }) && left.targetReservations.every(
    (fence, index) => fence.value === right.targetReservations[index]?.value
  );
}

function cloneAggregateRequest(request: ZLinkAggregatePrepareRequest): ZLinkAggregatePrepareRequest {
  return {
    ...request,
    aggregateId: { ...request.aggregateId },
    targetOwner: { ...request.targetOwner },
    inventoryDigest: Buffer.from(request.inventoryDigest),
    targetReservations: request.targetReservations.map(fence => ({ ...fence })),
    participants: request.participants.map(participant => ({
      ...participant,
      authorityKey: { ...participant.authorityKey },
      expectedStoreVersion: { ...participant.expectedStoreVersion },
      authorityPayload: Buffer.from(participant.authorityPayload),
      membershipMutation: Buffer.from(participant.membershipMutation)
    }))
  };
}

function requireText(value: string, name: string): string {
  if (value.length === 0 || value.includes('\0')) {
    throw new TypeError(`${name} must be non-empty text without NUL.`);
  }
  return value;
}
