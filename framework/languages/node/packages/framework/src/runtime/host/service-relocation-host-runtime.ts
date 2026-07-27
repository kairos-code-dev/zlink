import { randomUUID } from 'node:crypto';
import { SubmitResult } from '@zlink-systems/zlink';
import type {
  RoutingId,
  Type,
  ZLinkActor,
  ZLinkActorRelocationAdapter,
  ZLinkAuthorityKey,
  ZLinkAuthoritySnapshot,
  ZLinkLocationOwnerToken,
  ZLinkMeshNodeDescriptor,
  ZLinkLocationStore,
  ZLinkRelocationCapacityFence,
  ZLinkRelocationReference,
  ZLinkRelocationStore,
  ZLinkSpot,
  ZLinkSpotRelocationAdapter,
  ZLinkInstanceSpot
} from '../../contracts';
import type { ZLinkProviderResolver } from '../../contracts/Common/ZLinkProviderResolver';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ZLinkBackendMeshNode,
  ZLinkMeshCompletionTable
} from '../backend';
import type { ReceiveRecord } from '../foundation/service-runtime-contracts';
import {
  ServiceDurableRelocationRuntime,
  ServiceRelocationAuthorityPayloadCodec,
  crc32c,
  decodeServiceRelocationEnvelope,
  type ServiceRelocationEnvelope,
  type ServiceRelocationMembership,
  type ServiceRelocationParticipant,
  type ServiceRelocationPublication,
  type ServiceRelocationQueuedMessage,
  type ServiceRelocationStorePort,
  type ServiceRelocationSuccessorProgress,
  type ServiceRelocationTimer
} from '../foundation/service-relocation-runtime';
import {
  ServiceRelocationCoordinator,
  ServiceRelocationPostCommitError,
  type ServiceRelocationAuthorityCommit,
  type ServiceRelocationRestoreOwner
} from '../foundation/service-relocation-coordinator';
import {
  ServiceCapturedRelocationSourceCompletion,
  ServiceRelocationObjectCaptureOwner,
  ServiceRelocationObjectRestoreOwner,
  type ServiceCapturedObjectRelocation,
  type ServiceObjectRelocationStaging,
  type ServiceRelocationCaptureUnit,
  type ServiceRelocationHiddenObject,
  type ServiceRelocationTargetObjectPort
} from '../foundation/service-relocation-object-owner';
import {
  ServiceRelocationAggregateCommitter,
  type ServicePreparedRelocationAggregate,
  type ServiceRelocationAggregatePlan
} from '../foundation/service-relocation-aggregate-committer';
import { createProviderInstance } from '../spots/spot-provider';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkSpotActivation } from '../spots/spot-activation-state';
import type { DefaultZLinkActorManager } from '../actors';
import type { ZLinkActorRuntimeState, ZLinkRemoteBoundSessionTarget } from '../actors/actor-runtime-state';
import {
  replayActorHandoffBacklog,
  type ZLinkActorHandoffPacket,
  type ZLinkActorHandoffResult
} from '../actors/actor-handoff';
import { decodeHandoffBacklog } from '../spots/spot-remote-codec';
import type { ZLinkActorTransferRuntime } from './actor-transfer-runtime';
import { decodeAuthorityKey, encodeAuthorityKey } from '../locations/authority-key-codec';
import {
  decodeServiceRelocationControlRequest,
  decodeServiceRelocationControlResponse,
  encodeServiceRelocationControlRequest,
  encodeServiceRelocationControlResponse,
  type ZLinkServiceRelocationControlRequest,
  type ZLinkServiceRelocationControlResponse
} from './service-relocation-control';
import {
  decodeMaintenanceReplyRelay,
  decodeMaintenanceReplyRelayAck,
  encodeMaintenanceReplyRelay,
  encodeMaintenanceReplyRelayAck,
  encodeServiceWireFrozenActorApplicationRecord,
  M6bServiceWireCommand,
  type ServiceMaintenanceReplyRelay,
  type ServiceMaintenanceReplyRelayAck,
  type ServiceMaintenanceRelocationControl,
  type ServiceMaintenanceRelocationControlData,
  type ServiceMaintenanceRelocationPrepare,
  type ServiceWireRequestSourceFence,
  type ServiceWireRelocationCandidate,
  type ServiceWireRelocationCoordinatorFence,
  type ServiceWireRelocationObject,
  type ServiceWireRelocationParticipant
} from '../foundation/service-stateful-wire-codec';

const RELOCATION_TARGET_MESSAGE_CAPACITY = 64n;
const RELOCATION_TARGET_BYTE_CAPACITY = 256n * 1024n * 1024n;
const RELOCATION_TARGET_LIVE_LIMIT = 1024;
const RELOCATION_TARGET_TOMBSTONE_LIMIT = 1024;
const RELOCATION_TARGET_TOMBSTONE_TTL_MS = 5 * 60_000;

class TargetReservationRejectedError extends Error {}

interface ZLinkHostRelocationOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly providerResolver?: ZLinkProviderResolver;
  readonly locationStore: () => ZLinkLocationStore | undefined;
  readonly relocationStore: () => ZLinkRelocationStore | undefined;
  readonly currentOwner: () => ZLinkLocationOwnerToken | undefined;
  readonly liveDescriptors: (
    meshName: string,
    signal?: AbortSignal
  ) => Promise<readonly ZLinkMeshNodeDescriptor[]>;
  readonly meshNode: (meshName: string) => ZLinkBackendMeshNode | undefined;
  readonly completions: (meshName: string) => ZLinkMeshCompletionTable | undefined;
  readonly spotManager: () => DefaultZLinkSpotManager | undefined;
  readonly actorManager: () => DefaultZLinkActorManager | undefined;
  readonly actorTransfer: ZLinkActorTransferRuntime;
}

interface RemoteStaging {
  readonly id: string;
  readonly meshName: string;
  readonly targetNodeRid: string;
  readonly envelope: ServiceRelocationEnvelope;
  readonly relocation: { readonly high: bigint; readonly low: bigint };
  readonly targetAttemptGeneration: bigint;
  readonly candidate: ServiceWireRelocationCandidate;
  readonly object: ServiceWireRelocationObject;
  readonly participants: readonly ServiceWireRelocationParticipant[];
  readonly root: NonNullable<ServiceMaintenanceRelocationPrepare['root']>;
}

interface LocalHidden extends ServiceRelocationHiddenObject {
  readonly participant: ServiceRelocationParticipant;
  readonly actor?: ZLinkActor;
  readonly activation?: ZLinkSpotActivation;
  initialized: boolean;
  readonly restoredTimers: ServiceRelocationTimer[];
  readonly replayResults: ZLinkActorHandoffResult[];
  readonly replayPackets: ZLinkActorHandoffPacket[];
  readonly terminalDeliveries: Map<number, 'terminalReceived' | 'alreadyTerminal' | 'sourceLeaseExpired'>;
  targetAuthorityOwnerGeneration?: bigint;
}

interface LocalStage {
  readonly offer: TargetRelocationOffer;
  readonly owner: ServiceRelocationObjectRestoreOwner<LocalHidden>;
  readonly staging: ServiceObjectRelocationStaging<LocalHidden>;
  readonly coordinator: Extract<
    ServiceWireRelocationCoordinatorFence,
    ServiceWireRelocationCoordinatorFence
  >;
  readonly candidate: ServiceWireRelocationCandidate;
  phase: 'prepared' | 'published' | 'replayed' | 'sealed' | 'routed' | 'normalized' | 'open';
  terminalRelayed: boolean;
}

type TargetRelocationReservation =
  | { readonly kind: 'single'; readonly fence: ZLinkRelocationCapacityFence }
  | {
      readonly kind: 'aggregate';
      readonly prepared: ServicePreparedRelocationAggregate;
    };

interface TargetRelocationOffer {
  readonly prepare: ServiceMaintenanceRelocationPrepare;
  readonly prepareFingerprint: string;
  readonly authenticatedSourceNodeRid: string;
  readonly envelope: ServiceRelocationEnvelope;
  readonly offeredMessages: bigint;
  readonly offeredBytes: bigint;
  expiryTimer?: ReturnType<typeof setTimeout>;
  permit?: {
    readonly messages: bigint;
    readonly bytes: bigint;
    released: boolean;
  };
  reservation?: Promise<TargetRelocationReservation>;
  reconcileReservation?: (signal?: AbortSignal) => Promise<TargetRelocationReservation>;
  reservationCommitted?: boolean;
  materialization?: Promise<LocalStage>;
}

interface SourceActorSession {
  readonly state: ZLinkActorRuntimeState;
  readonly actor: ZLinkActor;
  readonly prepared: Awaited<ReturnType<ZLinkActorTransferRuntime['prepareMaintenanceSession']>>;
}

interface PendingRelocationControl {
  readonly targetNodeRid: string;
  readonly request: ZLinkServiceRelocationControlRequest;
  readonly resolve: (response: ZLinkServiceRelocationControlResponse) => void;
  readonly reject: (error: unknown) => void;
  readonly timer: ReturnType<typeof setInterval>;
}

interface PendingRelocationReplyRelay {
  readonly ackTargetNodeRid: string;
  readonly identityKey: string;
  readonly request: ServiceMaintenanceReplyRelay;
  readonly expectedRequestSource: ServiceWireRequestSourceFence;
  readonly resolve: (delivery: RelocationTerminalDelivery) => void;
  readonly reject: (error: unknown) => void;
  timer?: ReturnType<typeof setTimeout>;
}

/** Production host bridge from Retire inventory to remote RouteMesh owners. */
export class ZLinkHostServiceRelocationRuntime {
  private readonly targetOffers = new Map<string, TargetRelocationOffer>();
  private readonly targetStages = new Map<string, LocalStage>();
  private readonly targetAborts = new Map<string, {
    readonly fingerprint: string;
    readonly authenticatedSourceNodeRid: string;
    readonly expiresAtMs: number;
  }>();
  private readonly completedTargets = new Map<string, {
    readonly fingerprint: string;
    readonly authenticatedSourceNodeRid: string;
    readonly response: Extract<ServiceMaintenanceRelocationControl, { kind: 'complete' }>;
    readonly expiresAtMs: number;
  }>();
  private reservedTargetMessages = 0n;
  private reservedTargetBytes = 0n;
  private readonly relocationAuthorityKeys = new Map<string, string>();
  private readonly pendingControls = new Map<string, PendingRelocationControl>();
  private readonly pendingReplyRelays = new Map<string, PendingRelocationReplyRelay>();
  private readonly codec = new ServiceRelocationAuthorityPayloadCodec();

  constructor(private readonly options: ZLinkHostRelocationOptions) {}

  async dispose(): Promise<void> {
    const offers = new Set<TargetRelocationOffer>([
      ...this.targetOffers.values(),
      ...[...this.targetStages.values()].map(stage => stage.offer)
    ]);
    const errors: unknown[] = [];
    for (const offer of offers) {
      if (offer.expiryTimer !== undefined) clearTimeout(offer.expiryTimer);
      try {
        if (offer.reservationCommitted !== true) {
          await this.abortTargetOffer(relocationStagingId(offer.prepare), offer);
        } else {
          this.releaseTargetPermit(offer);
        }
      } catch (error) {
        errors.push(error);
      } finally {
        if (offer.expiryTimer !== undefined) clearTimeout(offer.expiryTimer);
      }
    }
    this.targetOffers.clear();
    this.targetStages.clear();
    this.targetAborts.clear();
    this.completedTargets.clear();
    const stopped = new Error('Relocation runtime stopped.');
    stopped.name = 'AbortError';
    for (const pending of this.pendingControls.values()) {
      clearInterval(pending.timer);
      pending.reject(stopped);
    }
    this.pendingControls.clear();
    for (const pending of this.pendingReplyRelays.values()) {
      if (pending.timer !== undefined) clearTimeout(pending.timer);
      pending.reject(stopped);
    }
    this.pendingReplyRelays.clear();
    this.relocationAuthorityKeys.clear();
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'Relocation runtime stop failed.');
  }

  async relocateMesh(meshName: string, signal?: AbortSignal): Promise<void> {
    const spotManager = this.requireSpotManager();
    const actorManager = this.requireActorManager();
    const localNode = this.requireMeshNode(meshName);
    const localStatus = localNode.status();
    const target = await this.selectTarget(meshName, String(localStatus.routingId), signal);
    const groupedActorIds = new Set<string>();

    for (const activation of spotManager.relocationActivations(meshName)) {
      const kind = this.spotKind(meshName, activation);
      if (kind === undefined) continue;
      const states = actorManager.snapshotStates().filter(state =>
        state.actor !== undefined && String(state.spotId) === String(activation.spotId));
      for (const state of states) groupedActorIds.add(state.actorId);
      await this.relocateSpotAggregate(meshName, activation, kind, states, target, signal);
    }

    for (const state of actorManager.snapshotStates()) {
      if (state.actor === undefined || groupedActorIds.has(state.actorId)) continue;
      if ((state.meshName ?? meshName) !== meshName) continue;
      await this.relocateStandaloneActor(meshName, state, target, signal);
    }
  }

  async tryHandleControl(
    meshName: string,
    record: ReceiveRecord,
    signal?: AbortSignal
  ): Promise<boolean> {
    if (record.parts.length !== 1) return false;
    const payload = record.parts[0]!.data();
    if (isServiceWireCommand(payload, M6bServiceWireCommand.replyRelay)) {
      await this.handleReplyRelay(
        meshName,
        decodeMaintenanceReplyRelay(payload),
        record.sourceNodeRid,
        signal
      );
      return true;
    }
    if (isServiceWireCommand(payload, M6bServiceWireCommand.replyRelayAck)) {
      await this.acceptReplyRelayAck(
        meshName,
        decodeMaintenanceReplyRelayAck(payload),
        record.sourceNodeRid,
        signal
      );
      return true;
    }
    const request = decodeServiceRelocationControlRequest(payload);
    if (request === undefined) return false;
    if (this.acceptControlResponse(request, record.sourceNodeRid)) return true;
    const response = await this.handleControl(meshName, request, record.sourceNodeRid, signal);
    if (record.sourceNodeRid === null) {
      throw new Error('Relocation control command has no authenticated source node.');
    }
    const submitted = this.requireMeshNode(meshName).sendToNode(
      record.sourceNodeRid,
      encodeServiceRelocationControlResponse(response)
    );
    if (submitted !== SubmitResult.Ok) {
      throw new Error('Relocation control reply was not accepted by RouteMesh.');
    }
    return true;
  }

  private async relocateSpotAggregate(
    meshName: string,
    activation: ZLinkSpotActivation,
    kind: 'user_spot' | 'instance_spot',
    actorStates: readonly ZLinkActorRuntimeState[],
    target: ZLinkMeshNodeDescriptor,
    signal?: AbortSignal
  ): Promise<void> {
    const store = this.requireLocationStore();
    const spotKey = encodeAuthorityKey(kind, String(activation.spotId));
    const spotAuthority = await requireAuthority(store, spotKey, signal);
    const actorAuthorities = new Map<string, ZLinkAuthoritySnapshot>();
    for (const state of actorStates) {
      actorAuthorities.set(
        state.actorId,
        await requireAuthority(store, encodeAuthorityKey('actor', state.actorId), signal)
      );
    }
    const sessions: SourceActorSession[] = [];
    let spotCapture: Awaited<ReturnType<ZLinkSpotActivation['captureRelocation']>> | undefined;
    let spotSealCommitted = false;
    const spotRegistration = this.spotRegistration(meshName, kind, spotAuthority.allocation.stableType);
    const spotUnit: ServiceRelocationCaptureUnit = {
      authorityKey: spotKey.value,
      objectKind: kind,
      stableType: spotAuthority.allocation.stableType,
      objectGeneration: spotAuthority.objectGeneration,
      authorityOwnerGeneration: spotAuthority.authorityOwnerGeneration,
      seal: async captureSignal => {
        for (const state of actorStates) {
          sessions.push({
            state,
            actor: state.actor!,
            prepared: await this.options.actorTransfer.prepareMaintenanceSession(
              state.actor!, state, captureSignal, false)
          });
        }
        spotCapture = await activation.captureRelocation(captureSignal);
        return {
          acceptedJournal: Buffer.alloc(0),
          queuedMessages: [],
          timers: spotCapture.timers.map(toServiceTimer)
        };
      },
      captureApplicationState: captureSignal =>
        this.captureApplication(spotRegistration.relocation, activation.spot, captureSignal),
      commitSeal: async () => {
        if (!spotSealCommitted) {
          if (spotCapture === undefined || !await activation.commitRelocation(spotCapture)) {
            throw new Error(`Spot '${String(activation.spotId)}' relocation seal became stale.`);
          }
          spotSealCommitted = true;
        }
        for (const session of sessions) {
          const committedAuthority = await requireAuthority(
            this.requireLocationStore(),
            encodeAuthorityKey('actor', session.state.actorId)
          );
          await session.prepared.commit({
            routerChannelId: meshName,
            targetNodeRid: target.rid,
            spotId: activation.spotId,
            spotKind: kind === 'user_spot' ? 2 : 3,
            authorityOwnerGeneration: committedAuthority.authorityOwnerGeneration
          } as never, {
            nodeRid: target.rid,
            actorId: session.state.actorId,
            generation: actorAuthorities.get(session.state.actorId)!.objectGeneration
          });
          activation.commitActorDeparture(session.state.actorId);
          this.requireActorManager().completeRelocationSource(session.state.actorId);
        }
        await this.requireSpotManager().completeRelocationSource(activation);
      },
      abortSeal: async () => {
        if (spotCapture !== undefined) activation.abortRelocation(spotCapture);
        for (const session of [...sessions].reverse()) await session.prepared.rollback();
      }
    };
    const actorUnits = actorStates.map(state => this.actorCaptureUnit(
      state,
      actorAuthorities.get(state.actorId)!,
      sessions
    ));
    const memberships = actorStates.map(state => ({
      actorKey: encodeAuthorityKey('actor', state.actorId).value,
      spotKey: spotKey.value,
      spotObjectGeneration: spotAuthority.objectGeneration,
      membershipEpoch: state.spotMembershipEpoch > 0n ? state.spotMembershipEpoch : 1n
    }));
    const captured = kind === 'user_spot'
      ? await new ServiceRelocationObjectCaptureOwner().captureUserSpotAggregate(
          randomUUID(), 1n, spotUnit, actorUnits, memberships, signal)
      : await new ServiceRelocationObjectCaptureOwner().captureInstanceSpotAggregate(
          randomUUID(), 1n, spotUnit, actorUnits, memberships, signal);
    await this.runCoordinator(
      meshName,
      target,
      spotAuthority,
      captured,
      new Map([[spotKey.value, spotAuthority], ...actorStates.map(state => [
        encodeAuthorityKey('actor', state.actorId).value,
        actorAuthorities.get(state.actorId)!
      ] as const)]),
      signal,
      sessions
    );
  }

  private async relocateStandaloneActor(
    meshName: string,
    state: ZLinkActorRuntimeState,
    target: ZLinkMeshNodeDescriptor,
    signal?: AbortSignal
  ): Promise<void> {
    const authority = await requireAuthority(
      this.requireLocationStore(),
      encodeAuthorityKey('actor', state.actorId),
      signal
    );
    const sessions: SourceActorSession[] = [];
    const unit = this.actorCaptureUnit(state, authority, sessions, true, target, meshName);
    const entrySpotId = target.entrySpotId ?? String(target.rid);
    const membership: ServiceRelocationMembership = {
      actorKey: encodeAuthorityKey('actor', state.actorId).value,
      spotKey: encodeAuthorityKey('user_spot', entrySpotId).value,
      spotObjectGeneration: target.lifecycleGeneration,
      membershipEpoch: state.spotMembershipEpoch > 0n ? state.spotMembershipEpoch : 1n
    };
    const captured = await new ServiceRelocationObjectCaptureOwner().captureStandaloneActor(
      randomUUID(), 1n, unit, membership, signal);
    await this.runCoordinator(
      meshName,
      target,
      authority,
      captured,
      new Map([[unit.authorityKey, authority]]),
      signal,
      sessions
    );
  }

  private actorCaptureUnit(
    state: ZLinkActorRuntimeState,
    authority: ZLinkAuthoritySnapshot,
    sessions: SourceActorSession[],
    standalone = false,
    target?: ZLinkMeshNodeDescriptor,
    meshName?: string
  ): ServiceRelocationCaptureUnit {
    const registration = this.actorRegistration(state.meshName ?? meshName ?? '', state.actorType ?? authority.allocation.stableType);
    let ownSession: SourceActorSession | undefined;
    return {
      authorityKey: encodeAuthorityKey('actor', state.actorId).value,
      objectKind: 'actor',
      stableType: authority.allocation.stableType,
      objectGeneration: authority.objectGeneration,
      authorityOwnerGeneration: authority.authorityOwnerGeneration,
      seal: async signal => {
        if (standalone) {
          ownSession = {
            state,
            actor: state.actor!,
            prepared: await this.options.actorTransfer.prepareMaintenanceSession(
              state.actor!, state, signal)
          };
          sessions.push(ownSession);
        }
        const prepared = standalone ? ownSession : sessions.find(value => value.state === state);
        return {
          acceptedJournal: encodeActorSession(prepared?.prepared.target),
          queuedMessages: encodeHandoffQueuedMessages(prepared?.prepared.handoffBacklog ?? []),
          timers: []
        };
      },
      captureApplicationState: signal =>
        this.captureApplication(registration.relocation, state.actor!, signal),
      commitSeal: async () => {
        if (!standalone || ownSession === undefined || target === undefined || meshName === undefined) return;
        const entrySpotId = target.entrySpotId ?? String(target.rid);
        const committedAuthority = await requireAuthority(
          this.requireLocationStore(),
          encodeAuthorityKey('actor', state.actorId)
        );
        await ownSession.prepared.commit({
          routerChannelId: meshName,
          targetNodeRid: target.rid,
          spotId: entrySpotId as RoutingId,
          spotKind: 1,
          authorityOwnerGeneration: committedAuthority.authorityOwnerGeneration
        } as never, {
          nodeRid: target.rid,
          actorId: state.actorId,
          generation: authority.objectGeneration
        });
        this.requireActorManager().completeRelocationSource(state.actorId);
      },
      abortSeal: async () => {
        if (standalone && ownSession !== undefined) await ownSession.prepared.rollback();
      }
    };
  }

  private async runCoordinator(
    meshName: string,
    target: ZLinkMeshNodeDescriptor,
    primary: ZLinkAuthoritySnapshot,
    captured: ServiceCapturedObjectRelocation,
    _authorities: ReadonlyMap<string, ZLinkAuthoritySnapshot>,
    signal?: AbortSignal,
    sessions: readonly SourceActorSession[] = []
  ): Promise<void> {
    const durable = new ServiceDurableRelocationRuntime(
      this.requireLocationStore(),
      relocationStorePort(this.requireRelocationStore()),
      this.codec
    );
    const localStatus = this.requireMeshNode(meshName).status();
    const controlDeadlineAtMs = Date.now() + 30_000;
    const remoteOwner = new RemoteRestoreOwner(
      meshName,
      String(target.rid),
      {
        ownerId: primary.ownerId,
        leaseGeneration: primary.ownerLeaseGeneration,
        nodeRid: String(localStatus.routingId),
        nodeGeneration: localStatus.lifecycleGeneration,
        expectedAuthorityStoreVersion: primary.storeVersion.value
      },
      {
        nodeRid: String(target.rid),
        nodeGeneration: target.lifecycleGeneration,
        ownerId: target.ownerId,
        ownerLeaseGeneration: target.leaseGeneration
      },
      request => this.sendControl(
        meshName,
        target.rid,
        request,
        signal,
        controlDeadlineAtMs
      )
    );
    const source = new ServiceCapturedRelocationSourceCompletion<RemoteStaging>(captured, {
      complete: async (_staging, authority, completionSignal) => {
        for (const session of sessions) {
          session.prepared.setReplayResults([]);
        }
        const pending = this.codec.read(authority.payload);
        if (pending === undefined) throw new Error('Pending relocation publication is missing.');
        const aggregate = captured.envelope.participants.length > 1;
        const retainedReference = aggregate && pending.phase === 'sourceCleanupPending'
          ? pending.reference
          : undefined;
        const completed = await durable.completeSourceCleanup(
          { value: primaryKey(captured.envelope) } as ZLinkAuthorityKey,
          authority,
          remoteOwner.successorProgress(captured.envelope),
          completionSignal,
          { retainPreviousRoot: retainedReference !== undefined }
        );
        await this.completeParticipantAuthorities(
          captured.envelope,
          primaryKey(captured.envelope),
          completed,
          completionSignal
        );
        if (retainedReference !== undefined) {
          await durable.deleteRetainedRoot(retainedReference, completionSignal);
        }
        return completed;
      }
    });
    const authorityCommit = this.authorityCommit(remoteOwner);
    const coordinator = new ServiceRelocationCoordinator(
      durable,
      remoteOwner,
      source,
      {
        relayCaptured: async (staging, _authority, relaySignal) => {
          remoteOwner.assertQueueReplayAcknowledged(captured.envelope);
          const previous = this.codec.read(_authority.payload);
          await remoteOwner.complete(staging, relaySignal);
          const delivered = await requireAuthority(
            this.requireLocationStore(),
            { value: primaryKey(captured.envelope) } as ZLinkAuthorityKey,
            relaySignal
          );
          await this.completeParticipantAuthorities(
            captured.envelope,
            primaryKey(captured.envelope),
            delivered,
            relaySignal
          );
          if (captured.envelope.participants.length > 1 && previous !== undefined) {
            await durable.deleteRetainedRoot(previous.reference, relaySignal);
          }
          return delivered;
        }
      },
      { replace: async () => undefined },
      {
        waitUntilReleasable: async (_staging, _authority, recoverySignal) =>
          this.releaseParticipantAuthorities(
            captured.envelope,
            primaryKey(captured.envelope),
            recoverySignal
          )
      },
      authorityCommit
    );
    let completed = false;
    let authorityCommitted = false;
    const relayAuthorityId = wireIdText(relocationWireId(captured.envelope.aggregateId));
    const relayAuthorityKey = primaryKey(captured.envelope);
    if (this.relocationAuthorityKeys.has(relayAuthorityId)) {
      throw new Error(`Relocation reply relay authority '${relayAuthorityId}' is already registered.`);
    }
    this.relocationAuthorityKeys.set(relayAuthorityId, relayAuthorityKey);
    try {
      const key = {
        value: remoteOwner.primaryAuthorityKey = primaryKey(captured.envelope)
      } as ZLinkAuthorityKey;
      let committed;
      try {
        committed = await coordinator.captureRestoreAndCommit(
          key,
          primary,
          { ownerId: target.ownerId, leaseGeneration: target.leaseGeneration },
          captured.envelope,
          undefined,
          signal
        );
      } catch (error) {
        if (!(error instanceof ServiceRelocationPostCommitError)) throw error;
        authorityCommitted = true;
        committed = await coordinator.resumeCommitted(key, {
          staging: error.staging as RemoteStaging,
          authority: error.authority
        }, signal);
      }
      if (committed.staging.id.length === 0) {
        throw new Error('Relocation target returned an empty staging identity.');
      }
      completed = true;
    } finally {
      if (!completed && !authorityCommitted) {
        await captured.abortSource().catch(() => undefined);
      }
      if (this.relocationAuthorityKeys.get(relayAuthorityId) === relayAuthorityKey) {
        this.relocationAuthorityKeys.delete(relayAuthorityId);
      }
    }
  }

  private authorityCommit(
    remoteOwner: RemoteRestoreOwner
  ): ServiceRelocationAuthorityCommit {
    return {
      commit: async (key, published, targetOwner, envelope, _fence, signal) => {
        let commitError: unknown;
        try {
          await remoteOwner.commitAuthority(envelope, signal);
        } catch (error) {
          commitError = error;
        }
        const current = await requireAuthority(this.requireLocationStore(), key, signal);
        const publication = this.codec.read(current.payload);
        if (current.ownerId !== targetOwner.ownerId
          || current.ownerLeaseGeneration !== targetOwner.leaseGeneration
          || current.authorityOwnerGeneration <= published.authorityOwnerGeneration
          || publication?.aggregateId !== envelope.aggregateId
          || publication.aggregateGeneration !== envelope.aggregateGeneration) {
          if (commitError !== undefined) throw commitError;
          throw new Error('Relocation target commit did not publish the exact owner fence.');
        }
        return current;
      }
    };
  }

  private async completeParticipantAuthorities(
    envelope: ServiceRelocationEnvelope,
    primaryKey: string,
    primary: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<void> {
    const completed = this.codec.read(primary.payload);
    if (completed === undefined) throw new Error('Completed relocation publication is missing.');
    for (const participant of envelope.participants) {
      if (participant.key === primaryKey) continue;
      const key = { value: participant.key } as ZLinkAuthorityKey;
      const current = await requireAuthority(this.requireLocationStore(), key, signal);
      const publication = this.codec.read(current.payload);
      if (publication === undefined) throw new Error(`Participant '${participant.key}' publication is missing.`);
      if (publication.reference === completed.reference
        && publication.aggregateGeneration === completed.aggregateGeneration) continue;
      if (publication.aggregateId !== completed.aggregateId
        || publication.aggregateGeneration + 1n !== completed.aggregateGeneration) {
        throw new Error(`Participant '${participant.key}' cleanup authority generation is stale.`);
      }
      const result = await this.requireLocationStore().compareExchangeAuthority(
        key,
        current.storeVersion,
        {
          kind: 'put',
          generationTransition: 'preserve',
          payload: this.codec.replace(current.payload, publication, completed)
        },
        signal
      );
      if (result.kind !== 'stored') {
        throw new Error(`Participant '${participant.key}' cleanup authority CAS failed.`);
      }
    }
  }

  private async releaseParticipantAuthorities(
    envelope: ServiceRelocationEnvelope,
    primaryKey: string,
    signal?: AbortSignal
  ): Promise<void> {
    for (const participant of envelope.participants) {
      if (participant.key === primaryKey) continue;
      const key = { value: participant.key } as ZLinkAuthorityKey;
      const current = await requireAuthority(this.requireLocationStore(), key, signal);
      const publication = this.codec.read(current.payload);
      if (publication === undefined) continue;
      const result = await this.requireLocationStore().compareExchangeAuthority(
        key,
        current.storeVersion,
        {
          kind: 'put',
          generationTransition: 'preserve',
          payload: this.codec.clear(current.payload, publication.reference)
        },
        signal
      );
      if (result.kind !== 'stored') {
        throw new Error(`Participant '${participant.key}' recovery pointer release failed.`);
      }
    }
  }

  private async handleControl(
    meshName: string,
    request: ZLinkServiceRelocationControlRequest,
    sourceNodeRid: RoutingId | null,
    signal?: AbortSignal
  ): Promise<ZLinkServiceRelocationControlResponse> {
    this.pruneTargetTombstones();
    const stagingId = relocationStagingId(request);
    if (request.kind === 'prepare') {
      if (sourceNodeRid === null || String(sourceNodeRid) !== request.sourceNodeRid) {
        throw new Error('Relocation prepare source node fence does not match the authenticated peer.');
      }
      const targetStatus = this.requireMeshNode(meshName).status();
      const targetOwner = this.options.currentOwner();
      if (targetOwner === undefined
        || String(targetStatus.routingId) !== request.candidate.nodeRid
        || targetStatus.lifecycleGeneration !== request.candidate.nodeGeneration
        || targetOwner.ownerId !== request.candidate.ownerId
        || targetOwner.leaseGeneration !== request.candidate.ownerLeaseGeneration) {
        throw new Error('Relocation prepare candidate fence does not match the target owner.');
      }
      if (request.root === undefined) throw new Error('Relocation prepare has no shared durable root.');
      const existing = this.targetStages.get(stagingId)?.offer
        ?? this.targetOffers.get(stagingId);
      if (existing !== undefined) {
        if (existing.authenticatedSourceNodeRid !== String(sourceNodeRid)) {
          throw new Error('Relocation prepare retry source node changed.');
        }
        validatePrepareOfferRetry(existing, request);
        return relocationCapacityOffer(
          request,
          existing.envelope,
          existing.offeredMessages,
          existing.offeredBytes
        );
      }
      const envelope = await this.readSharedEnvelope(request.root, signal);
      validateControlEnvelope(request, envelope);
      const raced = this.targetStages.get(stagingId)?.offer
        ?? this.targetOffers.get(stagingId);
      if (raced !== undefined) {
        if (raced.authenticatedSourceNodeRid !== String(sourceNodeRid)) {
          throw new Error('Relocation prepare retry source node changed.');
        }
        validatePrepareOfferRetry(raced, request);
        return relocationCapacityOffer(
          request,
          raced.envelope,
          raced.offeredMessages,
          raced.offeredBytes
        );
      }
      if (this.targetOffers.size + this.targetStages.size >= RELOCATION_TARGET_LIVE_LIMIT) {
        throw new Error('Relocation target live offer limit is exhausted.');
      }
      const offer = this.availableRelocationCapacity();
      const targetOffer: TargetRelocationOffer = {
        prepare: request,
        prepareFingerprint: stringifyWire(request),
        authenticatedSourceNodeRid: String(sourceNodeRid),
        envelope,
        offeredMessages: offer.messages,
        offeredBytes: offer.bytes
      };
      this.armTargetExpiry(stagingId, targetOffer);
      this.targetOffers.set(stagingId, targetOffer);
      return relocationCapacityOffer(request, envelope, offer.messages, offer.bytes);
    }
    if (request.kind === 'ready') {
      let stage = this.targetStages.get(stagingId);
      const offer = stage?.offer ?? this.targetOffers.get(stagingId);
      if (offer === undefined) {
        throw new Error(`Relocation offer '${stagingId}' is missing.`);
      }
      if (sourceNodeRid === null || String(sourceNodeRid) !== offer.authenticatedSourceNodeRid) {
        throw new Error('Relocation acceptance source does not match its prepare source.');
      }
      validateReadyAcceptance(offer, request);
      this.assertCurrentCandidate(meshName, offer.prepare.candidate);
      this.acquireTargetPermit(offer);
      try {
        await this.ensureTargetReservation(meshName, offer, signal);
      } catch (error) {
        this.releaseTargetPermit(offer);
        throw error;
      }
      this.armTargetExpiry(stagingId, offer);
      return { kind: 'reserved', relocation: request.relocation,
        targetAttemptGeneration: request.targetAttemptGeneration, round: request.round,
        coordinator: request.coordinator, candidate: request.candidate,
        reservationGeneration: request.reservationGeneration, participants: request.participants };
    }
    if (request.kind === 'data' && request.frozenRecord === undefined
      && (request.phase === 'prepared' || request.phase === 'committed'
        || request.phase === 'aborted')) {
      if (request.senderRole !== 'source') {
        throw new Error('Relocation staging control source does not match the coordinator fence.');
      }
      const offer = this.targetStages.get(stagingId)?.offer ?? this.targetOffers.get(stagingId);
      if (offer === undefined) {
        const aborted = this.targetAborts.get(stagingId);
        if (request.phase === 'aborted'
          && aborted?.fingerprint === stringifyWire(request)
          && sourceNodeRid !== null
          && aborted.authenticatedSourceNodeRid === String(sourceNodeRid)) {
          return relocationControlAck(request);
        }
        throw new Error(`Relocation offer '${stagingId}' is missing.`);
      }
      if (sourceNodeRid === null || String(sourceNodeRid) !== offer.authenticatedSourceNodeRid) {
        throw new Error('Relocation staging control source does not match its prepare source.');
      }
      validateStagingControl(offer, request);
      if (request.phase === 'aborted') {
        await this.abortTargetOffer(stagingId, offer, signal);
        this.targetAborts.set(stagingId, {
          fingerprint: stringifyWire(request),
          authenticatedSourceNodeRid: offer.authenticatedSourceNodeRid,
          expiresAtMs: Date.now() + RELOCATION_TARGET_TOMBSTONE_TTL_MS
        });
        this.trimTargetTombstones(this.targetAborts);
        return relocationControlAck(request);
      }
      const reservation = await this.ensureTargetReservation(meshName, offer, signal);
      let materialized: LocalStage;
      try {
        offer.materialization ??= this.materializeTargetOffer(meshName, stagingId, offer, signal);
        materialized = await offer.materialization;
      } catch (error) {
        await this.abortTargetOffer(stagingId, offer, signal).catch(() => undefined);
        throw error;
      }
      if (request.phase === 'committed') {
        await this.commitTargetReservation(materialized, reservation, signal);
        offer.reservationCommitted = true;
        if (offer.expiryTimer !== undefined) clearTimeout(offer.expiryTimer);
      }
      return relocationControlAck(request);
    }
    const stage = this.targetStages.get(stagingId);
    if (stage === undefined) {
      const completed = this.completedTargets.get(stagingId);
      if (request.kind === 'complete' && request.sourceCleanupState === 'completed'
        && completed?.fingerprint === stringifyWire(request)
        && sourceNodeRid !== null
        && completed.authenticatedSourceNodeRid === String(sourceNodeRid)) {
        return completed.response;
      }
      throw new Error(`Relocation staging '${stagingId}' is missing.`);
    }
    if (!sameCoordinator(stage.coordinator, request.coordinator)) {
      throw new Error('Relocation coordinator fence changed.');
    }
    if (sourceNodeRid === null || String(sourceNodeRid) !== stage.offer.authenticatedSourceNodeRid) {
      throw new Error('Relocation control source does not match its prepare source.');
    }
    if (request.kind === 'complete' && request.sourceCleanupState === 'pending') {
      if (relocationPhaseRank(stage.phase) >= relocationPhaseRank('published')) {
        return { ...request, senderRole: 'target' };
      }
      const authority = await requireAuthority(
        this.requireLocationStore(),
        { value: primaryKey(stage.staging.envelope) } as ZLinkAuthorityKey,
        signal
      );
      await stage.owner.publish(stage.staging, authority, signal);
      stage.phase = 'published';
      return { ...request, senderRole: 'target' };
    }
    if (request.kind === 'data') {
      const participantIndex = Number(request.participantId - 1n);
      if (!Number.isSafeInteger(participantIndex) || participantIndex < 0
        || participantIndex >= stage.staging.envelope.participants.length) {
        throw new Error('Relocation data participant is missing.');
      }
      const participant = stage.staging.envelope.participants[participantIndex]!;
      const queued = participant.queuedMessages.find(message => message.sequence === request.sequence);
      if (queued === undefined || request.frozenRecord === undefined) {
        throw new Error('Relocation data does not match a captured accepted record.');
      }
      const expectedRecord = canonicalQueuedFrozenRecord(
        participant,
        queued,
        stage.coordinator,
        stage.candidate
      );
      if (!request.frozenRecord.canonicalBytes.equals(expectedRecord.canonicalBytes)) {
        throw new Error('Relocation data frozen record changed after capture.');
      }
      if (relocationPhaseRank(stage.phase) < relocationPhaseRank('replayed')) {
        requireRelocationPhase(stage, 'published');
        await stage.owner.replayAcceptedJournal(stage.staging, signal);
        stage.phase = 'replayed';
      }
      return { kind: 'ack', relocation: request.relocation,
        targetAttemptGeneration: request.targetAttemptGeneration, coordinator: request.coordinator,
        senderRole: 'target', participantId: request.participantId,
        highWater: request.sequence };
    }
    if (request.kind === 'seal') {
      if (!request.response || request.senderRole !== 'source') {
        throw new Error('Relocation seal acknowledgement is invalid.');
      }
      if (stage.phase === 'published') {
        await stage.owner.replayAcceptedJournal(stage.staging, signal);
        stage.phase = 'replayed';
      }
      requireRelocationPhase(stage, 'replayed');
      stage.phase = 'sealed';
      this.releaseTargetPermit(stage.offer);
      return { ...request, senderRole: 'target' };
    }
    if (request.kind === 'complete' && request.sourceCleanupState === 'completed') {
      if (stage.phase !== 'open') {
        requireRelocationPhase(stage, 'sealed');
        await this.publishSessionRoutes(stage.staging);
        stage.phase = 'routed';
        let authority = await requireAuthority(
          this.requireLocationStore(),
          { value: primaryKey(stage.staging.envelope) } as ZLinkAuthorityKey,
          signal
        );
        const durable = new ServiceDurableRelocationRuntime(
          this.requireLocationStore(), relocationStorePort(this.requireRelocationStore()), this.codec);
        const pendingProgress = localSuccessorProgress(stage);
        if ([...pendingProgress.values()].some(value => value.terminalReplies.byteLength !== 0)) {
          const previous = this.codec.read(authority.payload);
          authority = await durable.advanceCompletedProgress(
            { value: primaryKey(stage.staging.envelope) } as ZLinkAuthorityKey,
            authority,
            pendingProgress,
            signal,
            { retainPreviousRoot: stage.staging.envelope.participants.length > 1 }
          );
          await this.completeParticipantAuthorities(
            stage.staging.envelope,
            primaryKey(stage.staging.envelope),
            authority,
            signal
          );
          if (stage.staging.envelope.participants.length > 1 && previous !== undefined) {
            await durable.deleteRetainedRoot(previous.reference, signal);
          }
        }
        if (!stage.terminalRelayed) {
          const completions = await this.readDurableTerminalCompletions(stage, authority, signal);
          await this.relayTerminalReplies(
            meshName,
            stage,
            this.targetReplyRelayCoordinator(meshName, stage, authority),
            completions,
            signal
          );
          stage.terminalRelayed = true;
        }
        const progress = localSuccessorProgress(stage);
        if ([...progress.values()].some(value => value.terminalReplies.byteLength !== 0)) {
          const previous = this.codec.read(authority.payload);
          authority = await durable.advanceCompletedProgress(
            { value: primaryKey(stage.staging.envelope) } as ZLinkAuthorityKey,
            authority,
            progress,
            signal,
            { retainPreviousRoot: stage.staging.envelope.participants.length > 1 }
          );
          await this.completeParticipantAuthorities(
            stage.staging.envelope,
            primaryKey(stage.staging.envelope),
            authority,
            signal
          );
          if (stage.staging.envelope.participants.length > 1 && previous !== undefined) {
            await durable.deleteRetainedRoot(previous.reference, signal);
          }
        }
        await stage.owner.normalize(stage.staging, authority, signal);
        stage.phase = 'normalized';
        await stage.owner.openAdmission(stage.staging, signal);
        stage.phase = 'open';
      }
      const response = { ...request, senderRole: 'target' as const };
      this.completedTargets.set(stagingId, {
        fingerprint: stringifyWire(request),
        authenticatedSourceNodeRid: stage.offer.authenticatedSourceNodeRid,
        response,
        expiresAtMs: Date.now() + RELOCATION_TARGET_TOMBSTONE_TTL_MS
      });
      this.trimTargetTombstones(this.completedTargets);
      if (stage.offer.expiryTimer !== undefined) clearTimeout(stage.offer.expiryTimer);
      this.releaseTargetPermit(stage.offer);
      this.targetStages.delete(stagingId);
      return response;
    }
    throw new Error(`Relocation command '${request.kind}' is invalid for target phase '${stage.phase}'.`);
  }

  private async readSharedEnvelope(
    root: NonNullable<ServiceMaintenanceRelocationPrepare['root']>,
    signal?: AbortSignal
  ): Promise<ServiceRelocationEnvelope> {
    const read = await this.requireRelocationStore().getRelocation(
      { value: root.reference } as ZLinkRelocationReference,
      signal
    );
    if (read.kind === 'missing' || crc32c(read.payload) !== root.checksumCrc32c) {
      throw new Error('Relocation prepare references missing or corrupt shared durable data.');
    }
    return decodeServiceRelocationEnvelope(read.payload);
  }

  private async handleReplyRelay(
    meshName: string,
    relay: ServiceMaintenanceReplyRelay,
    sourceNodeRid: RoutingId | null,
    signal?: AbortSignal
  ): Promise<void> {
    if (sourceNodeRid === null) {
      throw new Error('Relocation reply relay has no authenticated target node.');
    }
    await this.requireAdmittedPeer(
      meshName,
      String(sourceNodeRid),
      relay.coordinator,
      signal
    );
    const relocationId = wireIdText(relay.relocation);
    const authorityKey = this.relocationAuthorityKeys.get(relocationId);
    if (authorityKey === undefined) {
      throw new Error('Relocation reply relay has no registered durable authority.');
    }
    const authority = await requireAuthority(
      this.requireLocationStore(),
      { value: authorityKey } as ZLinkAuthorityKey,
      signal
    );
    const publication = this.codec.read(authority.payload);
    if (authority.storeVersion.value !== relay.coordinator.expectedAuthorityStoreVersion
      || publication?.phase !== 'sourceCleanupCompleted'
      || !sameWireId(relocationWireId(publication.aggregateId), relay.relocation)) {
      throw new Error('Relocation reply relay coordinator authority fence is stale.');
    }
    const accepted = this.options.actorTransfer.relayCanonicalMaintenanceTerminal(
      wireIdText(relay.operation),
      relay.replyRouteId.toString(),
      handoffResultFromRelay(relay),
      String(sourceNodeRid)
    );
    if (accepted.status !== 'terminalReceived' && accepted.status !== 'alreadyTerminal'
      || accepted.source === undefined) {
      throw new Error('Canonical relocation reply relay collided with another source route.');
    }
    const requestSource = handoffSourceFence(accepted.source);
    const localStatus = this.requireMeshNode(meshName).status();
    const localOwner = this.options.currentOwner();
    if (localOwner === undefined
      || requestSource.ownerId !== localOwner.ownerId
      || requestSource.leaseGeneration !== localOwner.leaseGeneration
      || requestSource.nodeRid !== String(localStatus.routingId)
      || requestSource.nodeGeneration !== localStatus.lifecycleGeneration) {
      throw new Error('Relocation reply relay request-source fence changed after capture.');
    }
    const ack: ServiceMaintenanceReplyRelayAck = {
      relocation: relay.relocation,
      coordinator: relay.coordinator,
      operation: relay.operation,
      requestSource,
      status: accepted.status
    };
    if (this.requireMeshNode(meshName).sendToNode(
      sourceNodeRid,
      encodeMaintenanceReplyRelayAck(ack)
    ) !== SubmitResult.Ok) {
      throw new Error('Relocation reply relay ACK send was rejected.');
    }
  }

  private async sendReplyRelay(
    meshName: string,
    ackTargetNodeRid: RoutingId,
    request: ServiceMaintenanceReplyRelay,
    expectedRequestSource: ServiceWireRequestSourceFence,
    signal?: AbortSignal
  ): Promise<RelocationTerminalDelivery> {
    const targetRid = String(ackTargetNodeRid);
    const identityKey = replyRelayIdentityKey(request);
    const key = replyRelayPendingKey(targetRid, request);
    if (this.pendingReplyRelays.has(key)) {
      throw new Error(`Relocation reply relay ACK '${key}' is already pending.`);
    }
    const node = this.requireMeshNode(meshName);
    return await new Promise<RelocationTerminalDelivery>((resolve, reject) => {
      let attempts = 0;
      const finish = (action: () => void) => {
        const pending = this.pendingReplyRelays.get(key);
        if (pending?.timer !== undefined) clearTimeout(pending.timer);
        this.pendingReplyRelays.delete(key);
        action();
      };
      const pending: PendingRelocationReplyRelay = {
        ackTargetNodeRid: targetRid,
        identityKey,
        request,
        expectedRequestSource,
        resolve: delivery => finish(() => resolve(delivery)),
        reject: error => finish(() => reject(error))
      };
      const send = async (): Promise<void> => {
        try {
          if (signal?.aborted === true) {
            pending.reject(signal.reason);
            return;
          }
          if (await this.exactSourceLeaseExpired(expectedRequestSource, signal)) {
            pending.resolve('sourceLeaseExpired');
            return;
          }
          if (++attempts > 120) {
            pending.reject(new Error(`Relocation reply relay ACK '${key}' timed out.`));
            return;
          }
          node.sendToNode(ackTargetNodeRid, encodeMaintenanceReplyRelay(request));
          if (this.pendingReplyRelays.get(key) === pending) {
            pending.timer = setTimeout(() => void send(), 250);
          }
        } catch (error) {
          pending.reject(error);
        }
      };
      this.pendingReplyRelays.set(key, pending);
      void send();
    });
  }

  private async acceptReplyRelayAck(
    meshName: string,
    ack: ServiceMaintenanceReplyRelayAck,
    sourceNodeRid: RoutingId | null,
    signal?: AbortSignal
  ): Promise<void> {
    const identityKey = replyRelayIdentityKey(ack);
    const exactKey = sourceNodeRid === null
      ? undefined
      : replyRelayPendingKey(String(sourceNodeRid), ack);
    const pending = exactKey === undefined ? undefined : this.pendingReplyRelays.get(exactKey);
    if (pending === undefined) {
      const collision = [...this.pendingReplyRelays.values()].find(
        value => value.identityKey === identityKey
      );
      collision?.reject(new Error('Relocation reply relay ACK target source collided.'));
      return;
    }
    try {
      await this.requireAdmittedPeer(
        meshName,
        pending.ackTargetNodeRid,
        pending.expectedRequestSource,
        signal
      );
      if (!sameWireId(ack.relocation, pending.request.relocation)
        || !sameWireId(ack.operation, pending.request.operation)
        || !sameCoordinator(ack.coordinator, pending.request.coordinator)
        || !sameRequestSource(ack.requestSource, pending.expectedRequestSource)) {
        throw new Error('Relocation reply relay ACK does not match its durable source fence.');
      }
      pending.resolve(ack.status);
    } catch (error) {
      pending.reject(error);
    }
  }

  private async requireAdmittedPeer(
    meshName: string,
    authenticatedRid: string,
    expected: ServiceWireRequestSourceFence | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    const descriptor = (await this.options.liveDescriptors(meshName, signal)).find(
      value => String(value.rid) === authenticatedRid && value.state === 1
    );
    if (descriptor === undefined
      || expected !== undefined && (
        descriptor.ownerId !== expected.ownerId
        || descriptor.leaseGeneration !== expected.leaseGeneration
        || String(descriptor.rid) !== expected.nodeRid
        || descriptor.lifecycleGeneration !== expected.nodeGeneration
      )) {
      throw new Error('Relocation reply relay peer is not the exact admitted source fence.');
    }
  }

  private targetReplyRelayCoordinator(
    meshName: string,
    stage: LocalStage,
    authority: ZLinkAuthoritySnapshot
  ): ServiceWireRelocationCoordinatorFence {
    const status = this.requireMeshNode(meshName).status();
    const owner = this.options.currentOwner();
    if (owner === undefined
      || owner.ownerId !== stage.candidate.ownerId
      || owner.leaseGeneration !== stage.candidate.ownerLeaseGeneration
      || String(status.routingId) !== stage.candidate.nodeRid
      || status.lifecycleGeneration !== stage.candidate.nodeGeneration) {
      throw new Error('Relocation reply relay target coordinator fence is stale.');
    }
    return {
      ownerId: owner.ownerId,
      leaseGeneration: owner.leaseGeneration,
      nodeRid: String(status.routingId),
      nodeGeneration: status.lifecycleGeneration,
      expectedAuthorityStoreVersion: authority.storeVersion.value
    };
  }

  private async relayTerminalReplies(
    meshName: string,
    stage: LocalStage,
    coordinator: ServiceWireRelocationCoordinatorFence,
    completions: readonly DurableRelocationTerminalCompletion[],
    signal?: AbortSignal
  ): Promise<void> {
    for (const completion of completions) {
      if (completion.delivery !== 'pending') continue;
      const hidden = stage.staging.hidden.get(completion.participantKey);
      if (hidden?.actor === undefined || hidden.targetAuthorityOwnerGeneration === undefined) {
        throw new Error('Durable relocation terminal participant is not a published Actor.');
      }
      const packet = hidden.replayPackets.find(value => value.index === completion.index);
      if (packet === undefined || !packet.returnResponse || packet.source === undefined
        || packet.operationId !== completion.operationId
        || !sameHandoffSource(packet.source, completion.source)) {
        throw new Error('Durable relocation terminal completion changed after replay.');
      }
      const request = maintenanceReplyRelay(
        stage,
        coordinator,
        completion.participantId,
        completion
      );
      const delivery = await this.sendReplyRelay(
        meshName,
        completion.source.nodeRid as RoutingId,
        request,
        handoffSourceFence(completion.source),
        signal
      );
      hidden.terminalDeliveries.set(completion.index, delivery);
    }
  }

  private async readDurableTerminalCompletions(
    stage: LocalStage,
    authority: ZLinkAuthoritySnapshot,
    signal?: AbortSignal
  ): Promise<readonly DurableRelocationTerminalCompletion[]> {
    const publication = this.codec.read(authority.payload);
    if (publication?.phase !== 'sourceCleanupCompleted') {
      throw new Error('Relocation terminal completion requires a completed durable root.');
    }
    const read = await this.requireRelocationStore().getRelocation(
      { value: publication.reference } as ZLinkRelocationReference,
      signal
    );
    if (read.kind !== 'found' || crc32c(read.payload) !== publication.checksumCrc32c) {
      throw new Error('Relocation terminal completion durable root is missing or corrupt.');
    }
    const envelope = decodeServiceRelocationEnvelope(read.payload);
    if (envelope.aggregateId !== stage.staging.envelope.aggregateId) {
      throw new Error('Relocation terminal completion durable identity changed.');
    }
    if (envelope.participants.length !== stage.staging.envelope.participants.length) {
      throw new Error('Relocation terminal completion participant count changed.');
    }
    const completions: DurableRelocationTerminalCompletion[] = [];
    for (const [index, participant] of envelope.participants.entries()) {
      const expected = stage.staging.envelope.participants[index]!;
      if (expected.key !== participant.key) {
        throw new Error('Relocation terminal completion participant order changed.');
      }
      const values = decodeTerminalCompletions(participant.terminalReplies);
      if (values.filter(value => value.delivery === 'pending').length
        !== participant.pendingRelayCount) {
        throw new Error('Relocation terminal completion pending relay count changed.');
      }
      for (const value of values) {
        completions.push({ ...value, participantKey: participant.key,
          participantId: BigInt(index + 1) });
      }
    }
    return completions;
  }

  private async exactSourceLeaseExpired(
    source: ServiceWireRequestSourceFence,
    signal?: AbortSignal
  ): Promise<boolean> {
    const lease = await this.requireLocationStore().readOwnerLease(source.ownerId, signal);
    return lease.kind === 'missing'
      || lease.token.ownerId !== source.ownerId
      || lease.token.leaseGeneration !== source.leaseGeneration
      || lease.leaseExpiresAt.getTime() <= lease.storeNow.getTime();
  }

  private async publishSessionRoutes(
    staging: ServiceObjectRelocationStaging<LocalHidden>
  ): Promise<void> {
    for (const hidden of staging.hidden.values()) {
      if (hidden.actor !== undefined) {
        await this.options.actorTransfer.publishRoutedActorOwnership(hidden.actor);
      }
    }
  }

  private async sendControl(
    meshName: string,
    targetNodeRid: RoutingId,
    request: ZLinkServiceRelocationControlRequest,
    signal?: AbortSignal,
    deadlineAtMs = Date.now() + 30_000
  ): Promise<ReturnType<typeof decodeServiceRelocationControlResponse>> {
    const node = this.requireMeshNode(meshName);
    const key = controlAckKey(request);
    if (this.pendingControls.has(key)) {
      throw new Error(`Relocation control ACK '${key}' is already pending.`);
    }
    return await new Promise<ZLinkServiceRelocationControlResponse>((resolve, reject) => {
      const send = () => {
        if (signal?.aborted === true) {
          finish(() => reject(signal.reason));
          return;
        }
        if (Date.now() >= deadlineAtMs) {
          finish(() => reject(new Error(`Relocation control ACK '${key}' timed out.`)));
          return;
        }
        node.sendToNode(targetNodeRid, encodeServiceRelocationControlRequest(request));
      };
      const timer = setInterval(send, 250);
      const finish = (action: () => void) => {
        clearInterval(timer);
        this.pendingControls.delete(key);
        action();
      };
      this.pendingControls.set(key, {
        targetNodeRid: String(targetNodeRid), request,
        resolve: response => finish(() => resolve(response)),
        reject: error => finish(() => reject(error)), timer
      });
      send();
    });
  }

  private acceptControlResponse(
    packet: ZLinkServiceRelocationControlRequest,
    sourceNodeRid: RoutingId | null
  ): boolean {
    const key = controlResponseKey(packet);
    if (key === undefined) return false;
    const pending = this.pendingControls.get(key);
    if (pending === undefined) return false;
    if (sourceNodeRid === null || String(sourceNodeRid) !== pending.targetNodeRid) {
      pending.reject(new Error('Relocation control ACK source node changed.'));
      return true;
    }
    try {
      const response = packet as ZLinkServiceRelocationControlResponse;
      validateControlResponse(pending.request, response);
      pending.resolve(response);
    } catch (error) {
      pending.reject(error);
    }
    return true;
  }

  private async selectTarget(
    meshName: string,
    localRid: string,
    signal?: AbortSignal
  ): Promise<ZLinkMeshNodeDescriptor> {
    const descriptors = await this.options.liveDescriptors(meshName, signal);
    const target = descriptors
      .filter(descriptor => String(descriptor.rid) !== localRid && descriptor.state === 1)
      .sort((left, right) => String(left.rid).localeCompare(String(right.rid))).at(0);
    if (target === undefined) throw new Error(`RouteMesh '${meshName}' has no relocation target.`);
    return target;
  }

  private pruneTargetTombstones(): void {
    const now = Date.now();
    for (const [key, value] of this.targetAborts) {
      if (value.expiresAtMs <= now) this.targetAborts.delete(key);
    }
    for (const [key, value] of this.completedTargets) {
      if (value.expiresAtMs <= now) this.completedTargets.delete(key);
    }
  }

  private trimTargetTombstones<T extends { readonly expiresAtMs: number }>(
    values: Map<string, T>
  ): void {
    while (values.size > RELOCATION_TARGET_TOMBSTONE_LIMIT) {
      const oldest = values.keys().next().value;
      if (oldest === undefined) return;
      values.delete(oldest);
    }
  }

  private availableRelocationCapacity(): { readonly messages: bigint; readonly bytes: bigint } {
    const messages = RELOCATION_TARGET_MESSAGE_CAPACITY - this.reservedTargetMessages;
    const bytes = RELOCATION_TARGET_BYTE_CAPACITY - this.reservedTargetBytes;
    if (messages <= 0n || bytes <= 0n) {
      throw new Error('Relocation target replay capacity is exhausted.');
    }
    return { messages, bytes };
  }

  private acquireTargetPermit(offer: TargetRelocationOffer): void {
    const existing = offer.permit;
    if (existing !== undefined && !existing.released) return;
    const messages = existing?.messages ?? offer.prepare.participants.reduce(
      (sum, participant) => sum + participant.allowanceMessages,
      0n
    );
    const bytes = existing?.bytes ?? offer.prepare.participants.reduce(
      (sum, participant) => sum + participant.allowanceBytes,
      0n
    );
    const available = this.availableRelocationCapacity();
    if (messages > available.messages || bytes > available.bytes) {
      throw new Error('Relocation target replay capacity is exhausted.');
    }
    if (existing === undefined) {
      offer.permit = { messages, bytes, released: false };
    } else {
      existing.released = false;
    }
    this.reservedTargetMessages += messages;
    this.reservedTargetBytes += bytes;
  }

  private releaseTargetPermit(offer: TargetRelocationOffer): void {
    const permit = offer.permit;
    if (permit === undefined || permit.released) return;
    permit.released = true;
    this.reservedTargetMessages -= permit.messages;
    this.reservedTargetBytes -= permit.bytes;
  }

  private assertCurrentCandidate(
    meshName: string,
    candidate: ServiceWireRelocationCandidate
  ): void {
    const status = this.requireMeshNode(meshName).status();
    const owner = this.options.currentOwner();
    if (owner === undefined
      || String(status.routingId) !== candidate.nodeRid
      || status.lifecycleGeneration !== candidate.nodeGeneration
      || owner.ownerId !== candidate.ownerId
      || owner.leaseGeneration !== candidate.ownerLeaseGeneration) {
      throw new Error('Relocation candidate fence does not match the current target owner.');
    }
  }

  private async ensureTargetReservation(
    meshName: string,
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<TargetRelocationReservation> {
    offer.reservation ??= this.reserveTargetOffer(meshName, offer, signal);
    try {
      return await offer.reservation;
    } catch (firstError) {
      if (firstError instanceof TargetReservationRejectedError
        || offer.reconcileReservation === undefined) {
        throw firstError;
      }
      offer.reservation = offer.reconcileReservation(signal);
      try {
        return await offer.reservation;
      } catch {
        throw firstError;
      }
    }
  }

  private async reserveTargetOffer(
    meshName: string,
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<TargetRelocationReservation> {
    this.assertCurrentCandidate(meshName, offer.prepare.candidate);
    const authorities = await this.readTargetParticipantAuthorities(offer, signal);
    if (!isUserSpotAggregate(offer.envelope)) {
      const participant = offer.envelope.participants[0]!;
      const expected = authorities.get(participant.key)!;
      const request = {
        reservationId: offer.envelope.aggregateId,
        authorityKey: { value: participant.key } as ZLinkAuthorityKey,
        expectedStoreVersion: expected.storeVersion,
        objectKind: expected.allocation.objectKind,
        stableType: expected.allocation.stableType,
        sourceDescriptor: expected.allocation.descriptor,
        sourceNodeLifecycleGeneration: expected.allocation.descriptorLifecycleGeneration,
        sourceOwner: {
          ownerId: expected.ownerId,
          leaseGeneration: expected.ownerLeaseGeneration
        },
        targetDescriptor: { meshName, rid: offer.prepare.candidate.nodeRid as RoutingId },
        targetNodeLifecycleGeneration: offer.prepare.candidate.nodeGeneration,
        targetOwner: {
          ownerId: offer.prepare.candidate.ownerId,
          leaseGeneration: offer.prepare.candidate.ownerLeaseGeneration
        },
        capacity: expected.allocation.capacity
      };
      offer.reconcileReservation = async reconcileSignal =>
        await this.reserveExactSingleTarget(request, reconcileSignal);
      return await offer.reconcileReservation(signal);
    }
    const committer = new ServiceRelocationAggregateCommitter(this.requireLocationStore());
    const plan = this.targetAggregatePlan(meshName, offer, authorities);
    offer.reconcileReservation = async reconcileSignal => ({
      kind: 'aggregate',
      prepared: await committer.prepare(plan, reconcileSignal)
    });
    return await offer.reconcileReservation(signal);
  }

  private async reserveExactSingleTarget(
    request: Parameters<ZLinkLocationStore['reserveRelocationCapacity']>[0],
    signal?: AbortSignal
  ): Promise<TargetRelocationReservation> {
    const result = await this.requireLocationStore().reserveRelocationCapacity(request, signal);
    if (result.kind !== 'reserved' && result.kind !== 'alreadyReserved') {
      throw new TargetReservationRejectedError(
        'Relocation target capacity reservation failed with ' + result.kind + '.'
      );
    }
    return { kind: 'single', fence: result.fence };
  }

  private armTargetExpiry(stagingId: string, offer: TargetRelocationOffer): void {
    if (offer.expiryTimer !== undefined) clearTimeout(offer.expiryTimer);
    offer.expiryTimer = setTimeout(() => {
      void this.expireTargetOffer(stagingId, offer);
    }, 30_000);
    offer.expiryTimer.unref();
  }

  private async expireTargetOffer(stagingId: string, offer: TargetRelocationOffer): Promise<void> {
    if (this.targetOffers.get(stagingId) !== offer
      && this.targetStages.get(stagingId)?.offer !== offer) return;
    if (offer.reservation === undefined) {
      this.targetOffers.delete(stagingId);
      this.releaseTargetPermit(offer);
      return;
    }
    const authority = await this.requireLocationStore().readAuthority(
      { value: primaryKey(offer.envelope) } as ZLinkAuthorityKey
    ).catch(() => undefined);
    const publication = authority?.kind === 'snapshot'
      ? this.codec.read(authority.payload)
      : undefined;
    if (authority?.kind === 'snapshot'
      && authority.ownerId === offer.prepare.candidate.ownerId
      && authority.ownerLeaseGeneration === offer.prepare.candidate.ownerLeaseGeneration
      && publication?.aggregateId === offer.envelope.aggregateId
      && publication.aggregateGeneration === offer.envelope.aggregateGeneration) {
      offer.reservationCommitted = true;
      this.releaseTargetPermit(offer);
      return;
    }
    await this.abortTargetOffer(stagingId, offer).catch(() => undefined);
  }

  private async readTargetParticipantAuthorities(
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<ReadonlyMap<string, ZLinkAuthoritySnapshot>> {
    const authorities = new Map<string, ZLinkAuthoritySnapshot>();
    for (const participant of offer.envelope.participants) {
      const current = await requireAuthority(
        this.requireLocationStore(),
        { value: participant.key } as ZLinkAuthorityKey,
        signal
      );
      if (current.objectGeneration !== participant.objectGeneration
        || current.authorityOwnerGeneration !== participant.authorityOwnerGeneration
        || current.allocation.objectKind !== participant.objectKind
        || current.allocation.stableType !== participant.stableType
        || current.allocation.state !== 'active') {
        throw new Error(`Relocation participant '${participant.key}' authority changed before reservation.`);
      }
      authorities.set(participant.key, current);
    }
    const primary = authorities.get(primaryKey(offer.envelope))!;
    const publication = this.codec.read(primary.payload);
    if (publication?.phase !== 'sourceCleanupPending'
      || publication.reference !== offer.prepare.root?.reference
      || publication.checksumCrc32c !== offer.prepare.root.checksumCrc32c
      || publication.aggregateId !== offer.envelope.aggregateId
      || publication.aggregateGeneration !== offer.envelope.aggregateGeneration
      || publication.targetOwnerId !== offer.prepare.candidate.ownerId
      || publication.targetOwnerLeaseGeneration !== offer.prepare.candidate.ownerLeaseGeneration) {
      throw new Error('Relocation target reservation has no exact captured authority publication.');
    }
    return authorities;
  }

  private targetAggregatePlan(
    meshName: string,
    offer: TargetRelocationOffer,
    authorities: ReadonlyMap<string, ZLinkAuthoritySnapshot>
  ): ServiceRelocationAggregatePlan {
    const primaryAuthorityKey = primaryKey(offer.envelope);
    const publication = this.codec.read(authorities.get(primaryAuthorityKey)!.payload)!;
    const participants = offer.envelope.participants.map(participant => {
      const expected = authorities.get(participant.key)!;
      return {
        key: { value: participant.key } as ZLinkAuthorityKey,
        expected,
        ownerTransition: 'newOwner' as const,
        authorityPayload: participant.key === primaryAuthorityKey
          ? expected.payload
          : this.codec.publish(expected.payload, publication),
        membershipMutation: encodeMembershipMutation(offer.envelope.memberships, participant.key)
      };
    });
    return {
      envelope: offer.envelope,
      participants,
      targetDescriptor: { meshName, rid: offer.prepare.candidate.nodeRid as RoutingId },
      targetDescriptorLifecycleGeneration: offer.prepare.candidate.nodeGeneration,
      capacity: participants.reduce((sum, participant) =>
        addCapacity(sum, participant.expected.allocation.capacity), { actors: 0, spots: 0 }),
      targetOwner: {
        ownerId: offer.prepare.candidate.ownerId,
        leaseGeneration: offer.prepare.candidate.ownerLeaseGeneration
      }
    };
  }

  private async abortTargetOffer(
    stagingId: string,
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<void> {
    if (offer.reservationCommitted === true) {
      throw new Error('Committed relocation target reservation cannot be aborted.');
    }
    let durableAbortCompleted = false;
    try {
      const stage = this.targetStages.get(stagingId)
        ?? (offer.materialization === undefined
          ? undefined
          : await offer.materialization.catch(() => undefined));
      if (stage !== undefined) await stage.owner.abort(stage.staging);
      const reservation = await this.reconcileTargetReservationForAbort(offer, signal);
      if (reservation?.kind === 'aggregate') {
        await new ServiceRelocationAggregateCommitter(this.requireLocationStore())
          .abort(reservation.prepared, signal);
      } else if (reservation?.kind === 'single') {
        const result = await this.requireLocationStore().abortRelocationCapacity(
          reservation.fence,
          signal
        );
        if (result !== 'aborted' && result !== 'alreadyAborted') {
          throw new Error('Relocation target capacity abort failed with ' + result + '.');
        }
      }
      durableAbortCompleted = true;
    } finally {
      if (durableAbortCompleted) {
        if (offer.expiryTimer !== undefined) clearTimeout(offer.expiryTimer);
        this.releaseTargetPermit(offer);
        this.targetStages.delete(stagingId);
        this.targetOffers.delete(stagingId);
      } else if (this.targetOffers.get(stagingId) === offer
        || this.targetStages.get(stagingId)?.offer === offer) {
        this.armTargetExpiry(stagingId, offer);
      }
    }
  }

  private async reconcileTargetReservationForAbort(
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<TargetRelocationReservation | undefined> {
    if (offer.reservation === undefined) return undefined;
    try {
      return await offer.reservation;
    } catch (error) {
      if (error instanceof TargetReservationRejectedError) return undefined;
      if (offer.reconcileReservation === undefined) throw error;
      return await offer.reconcileReservation(signal);
    }
  }

  private async commitTargetReservation(
    stage: LocalStage,
    reservation: TargetRelocationReservation,
    signal?: AbortSignal
  ): Promise<void> {
    if (reservation.kind === 'aggregate') {
      await new ServiceRelocationAggregateCommitter(this.requireLocationStore())
        .commit(reservation.prepared, signal);
      stage.offer.reservationCommitted = true;
      return;
    }
    const key = stage.staging.primaryAuthorityKey;
    const current = await requireAuthority(this.requireLocationStore(), key, signal);
    const publication = this.codec.read(current.payload);
    if (current.ownerId === stage.candidate.ownerId
      && current.ownerLeaseGeneration === stage.candidate.ownerLeaseGeneration
      && publication?.aggregateId === stage.staging.envelope.aggregateId
      && publication.aggregateGeneration === stage.staging.envelope.aggregateGeneration) {
      return;
    }
    const durable = new ServiceDurableRelocationRuntime(
      this.requireLocationStore(),
      relocationStorePort(this.requireRelocationStore()),
      this.codec
    );
    await durable.commitOwner(
      key,
      current,
      {
        ownerId: stage.candidate.ownerId,
        leaseGeneration: stage.candidate.ownerLeaseGeneration
      },
      reservation.fence,
      signal
    );
    stage.offer.reservationCommitted = true;
  }

  private async materializeTargetOffer(
    meshName: string,
    stagingId: string,
    offer: TargetRelocationOffer,
    signal?: AbortSignal
  ): Promise<LocalStage> {
    const target = new LocalTargetPort(this.options, meshName, offer.envelope);
    const owner = new ServiceRelocationObjectRestoreOwner(
      target,
      value => ({ value } as ZLinkAuthorityKey)
    );
    const staging = await owner.prepare(offer.envelope, signal);
    if (staging.id !== `${offer.envelope.aggregateId}:${offer.envelope.aggregateGeneration}`) {
      throw new Error('Relocation materialization returned a different staging identity.');
    }
    const stage: LocalStage = {
      offer,
      owner,
      staging,
      coordinator: offer.prepare.coordinator,
      candidate: offer.prepare.candidate,
      phase: 'prepared',
      terminalRelayed: false
    };
    this.targetStages.set(stagingId, stage);
    this.targetOffers.delete(stagingId);
    return stage;
  }

  private spotKind(
    meshName: string,
    activation: ZLinkSpotActivation
  ): 'user_spot' | 'instance_spot' | undefined {
    const node = this.options.registration.spotNodes.get(meshName);
    if (Object.values(node?.spotFactoryRegistrations ?? {})
      .some(value => value.implementation === activation.spotType)) return 'user_spot';
    if (Object.values(node?.instanceSpotFactoryRegistrations ?? {})
      .some(value => value.implementation === activation.spotType)) return 'instance_spot';
    return undefined;
  }

  private spotRegistration(
    meshName: string,
    kind: 'user_spot' | 'instance_spot',
    stableType: string
  ) {
    const node = this.options.registration.spotNodes.get(meshName);
    const value = kind === 'user_spot'
      ? node?.spotFactoryRegistrations?.[stableType]
      : node?.instanceSpotFactoryRegistrations?.[stableType];
    if (value === undefined) throw new Error(`Relocation Spot type '${stableType}' is not registered.`);
    return value;
  }

  private actorRegistration(meshName: string, stableType: string) {
    const value = this.options.registration.spotNodes.get(meshName)
      ?.actorFactoryRegistrations?.[stableType];
    if (value === undefined) throw new Error(`Relocation Actor type '${stableType}' is not registered.`);
    return value;
  }

  private async captureApplication<T>(
    policy: { readonly kind: 'disabled' | 'recreate' | 'snapshot'; readonly adapterType?: Type },
    value: T,
    signal?: AbortSignal
  ): Promise<Uint8Array> {
    if (policy.kind === 'disabled') {
      throw new Error(`Relocation is disabled for '${(value as { constructor?: { name?: string } }).constructor?.name ?? 'object'}'.`);
    }
    if (policy.kind === 'recreate') return Buffer.alloc(0);
    const adapter = await createProviderInstance(policy.adapterType!, this.options.providerResolver) as
      ZLinkActorRelocationAdapter<ZLinkActor> | ZLinkSpotRelocationAdapter<ZLinkSpot | ZLinkInstanceSpot>;
    return Buffer.from(await adapter.capture(value as never, signal ?? new AbortController().signal));
  }

  private requireLocationStore() {
    const value = this.options.locationStore();
    if (value === undefined) throw new Error('Host relocation requires a Location Store.');
    return value;
  }

  private requireRelocationStore(): ZLinkRelocationStore {
    const value = this.options.relocationStore();
    if (value === undefined) throw new Error('Host relocation requires a Relocation Store.');
    return value;
  }

  private requireSpotManager(): DefaultZLinkSpotManager {
    const value = this.options.spotManager();
    if (value === undefined) throw new Error('Host relocation requires the Spot manager.');
    return value;
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const value = this.options.actorManager();
    if (value === undefined) throw new Error('Host relocation requires the Actor manager.');
    return value;
  }

  private requireMeshNode(meshName: string): ZLinkBackendMeshNode {
    const value = this.options.meshNode(meshName);
    if (value === undefined) throw new Error(`RouteMesh '${meshName}' is not started.`);
    return value;
  }
}

class RemoteRestoreOwner implements ServiceRelocationRestoreOwner<RemoteStaging> {
  primaryAuthorityKey?: string;
  private readonly highWater = new Map<bigint, bigint>();
  private staging?: RemoteStaging;

  constructor(
    private readonly meshName: string,
    private readonly targetNodeRid: string,
    private readonly coordinator: ServiceWireRelocationCoordinatorFence,
    private readonly candidate: ServiceWireRelocationCandidate,
    private readonly request: (
      request: ZLinkServiceRelocationControlRequest
    ) => Promise<ReturnType<typeof decodeServiceRelocationControlResponse>>
  ) {}

  async prepare(
    envelope: ServiceRelocationEnvelope,
    _signal?: AbortSignal,
    publication?: ServiceRelocationPublication
  ): Promise<RemoteStaging> {
    if (publication === undefined) throw new Error('Relocation prepare has no durable publication.');
    const id = `${envelope.aggregateId}:${envelope.aggregateGeneration}`;
    const relocation = relocationWireId(envelope.aggregateId);
    const object = relocationObject(envelope);
    const participants = relocationParticipants(envelope, this.coordinator, this.candidate);
    const root = { reference: publication.reference, checksumCrc32c: publication.checksumCrc32c };
    const requiredMessages = participants.reduce((sum, value) => sum + value.allowanceMessages, 0n);
    const requiredBytes = participants.reduce((sum, value) => sum + value.allowanceBytes, 0n);
    const prepare: ServiceMaintenanceRelocationPrepare = {
      kind: 'prepare', relocation, targetAttemptGeneration: envelope.aggregateGeneration,
      round: 'initial', coordinator: this.coordinator, candidate: this.candidate,
      initiatorRole: 'source', object, sourceNodeRid: this.coordinator.nodeRid,
      sourceNodeGeneration: this.coordinator.nodeGeneration, requiredMessages, requiredBytes,
      participants, root, applicationVersion: 1n
    };
    const offered = await this.request(prepare);
    if (offered.kind !== 'ready' || offered.role !== 'target'
      || offered.offeredMessages < requiredMessages
      || offered.offeredBytes < requiredBytes) {
      throw new Error('Relocation target did not return a capacity offer.');
    }
    const reserved = await this.request({
      ...offered,
      role: 'source',
      offeredMessages: 0n,
      offeredBytes: 0n,
      participants
    });
    if (reserved.kind !== 'reserved'
      || reserved.reservationGeneration !== offered.reservationGeneration) {
      throw new Error('Relocation target did not acknowledge the reservation.');
    }
    const staging = { id, meshName: this.meshName, targetNodeRid: this.targetNodeRid, envelope,
      relocation, targetAttemptGeneration: envelope.aggregateGeneration,
      candidate: this.candidate, object, participants, root };
    try {
      const prepared = await this.request(this.stagingControl(staging, 'prepared'));
      if (prepared.kind !== 'ack' || prepared.participantId !== 1n || prepared.highWater !== 1n) {
        throw new Error('Relocation target did not acknowledge prepared staging.');
      }
      this.staging = staging;
      return staging;
    } catch (error) {
      await this.request(this.stagingControl(staging, 'aborted')).catch(() => undefined);
      throw error;
    }
  }

  async publish(staging: RemoteStaging): Promise<void> {
    const response = await this.request(this.completion(staging, 'pending'));
    if (response.kind !== 'complete' || response.sourceCleanupState !== 'pending') {
      throw new Error('Relocation target did not publish the reserved owner.');
    }
  }

  async replayAcceptedJournal(staging: RemoteStaging): Promise<void> {
    for (const [index, participant] of staging.participants.entries()) {
      const captured = staging.envelope.participants[index]!;
      let highWater = captured.replayCursor;
      for (const message of captured.queuedMessages) {
        const frozenRecord = canonicalQueuedFrozenRecord(
          captured,
          message,
          this.coordinator,
          staging.candidate
        );
        const response = await this.request({ kind: 'data', relocation: staging.relocation,
          targetAttemptGeneration: staging.targetAttemptGeneration, coordinator: this.coordinator,
          senderRole: 'source', participantId: participant.participantId,
          sequence: message.sequence, frozenRecord });
        if (response.kind !== 'ack' || response.participantId !== participant.participantId
          || response.highWater !== message.sequence) {
          throw new Error('Relocation target returned an invalid replay high-water ACK.');
        }
        highWater = response.highWater;
      }
      this.highWater.set(participant.participantId, highWater);
    }
    const response = await this.request({ kind: 'seal', relocation: staging.relocation,
      targetAttemptGeneration: staging.targetAttemptGeneration, coordinator: this.coordinator,
      senderRole: 'source', response: true,
      participants: staging.participants.map(value => ({ participantId: value.participantId,
        highWater: this.highWater.get(value.participantId) ?? 0n })) });
    if (response.kind !== 'seal' || response.senderRole !== 'target') {
      throw new Error('Relocation target did not close the replay seal.');
    }
  }

  async normalize(): Promise<void> {}

  async openAdmission(): Promise<void> {}

  async abort(staging: RemoteStaging): Promise<void> {
    const response = await this.request(this.stagingControl(staging, 'aborted'));
    if (response.kind !== 'ack' || response.participantId !== 1n || response.highWater !== 1n) {
      throw new Error('Relocation target did not acknowledge precommit abort cleanup.');
    }
    if (this.staging?.id === staging.id) this.staging = undefined;
  }

  async commitAuthority(
    envelope: ServiceRelocationEnvelope,
    _signal?: AbortSignal
  ): Promise<void> {
    const staging = this.staging;
    if (staging === undefined || staging.envelope.aggregateId !== envelope.aggregateId
      || staging.envelope.aggregateGeneration !== envelope.aggregateGeneration) {
      throw new Error('Relocation target commit has no exact prepared staging.');
    }
    const response = await this.request(this.stagingControl(staging, 'committed'));
    if (response.kind !== 'ack' || response.participantId !== 1n || response.highWater !== 1n) {
      throw new Error('Relocation target did not acknowledge owner commit.');
    }
  }

  async complete(
    staging: RemoteStaging,
    _signal?: AbortSignal
  ): Promise<void> {
    const response = await this.request(this.completion(staging, 'completed'));
    if (response.kind !== 'complete' || response.sourceCleanupState !== 'completed') {
      throw new Error('Relocation target did not acknowledge source cleanup completion.');
    }
  }

  assertQueueReplayAcknowledged(envelope: ServiceRelocationEnvelope): void {
    for (const [index, participant] of envelope.participants.entries()) {
      const expected = participant.queuedMessages.at(-1)?.sequence ?? participant.replayCursor;
      if (this.highWater.get(BigInt(index + 1)) !== expected) {
        throw new Error(`Participant '${participant.key}' relocation replay acknowledgement is incomplete.`);
      }
    }
  }

  successorProgress(envelope: ServiceRelocationEnvelope): ServiceRelocationSuccessorProgress {
    const progress = new Map<string, import('../foundation/service-relocation-runtime').ServiceRelocationParticipantProgress>();
    for (const [index, participant] of envelope.participants.entries()) {
      progress.set(participant.key, {
        replayCursor: this.highWater.get(BigInt(index + 1)) ?? participant.replayCursor,
        terminalReplies: Buffer.alloc(0),
        pendingRelayCount: 0
      });
    }
    return progress;
  }

  private completion(
    staging: RemoteStaging,
    state: 'pending' | 'completed'
  ): Extract<ServiceMaintenanceRelocationControl, { kind: 'complete' }> {
    return { kind: 'complete', relocation: staging.relocation,
      targetAttemptGeneration: staging.targetAttemptGeneration, coordinator: this.coordinator,
      senderRole: 'source', source: coordinatorSource(this.coordinator), sourceCleanupState: state };
  }

  private stagingControl(
    staging: RemoteStaging,
    phase: 'prepared' | 'committed' | 'aborted'
  ): Extract<ServiceMaintenanceRelocationControl, { kind: 'data'; frozenRecord?: undefined }> {
    return {
      kind: 'data',
      relocation: staging.relocation,
      targetAttemptGeneration: staging.targetAttemptGeneration,
      coordinator: this.coordinator,
      senderRole: 'source',
      participantId: 1n,
      sequence: 1n,
      source: coordinatorSource(this.coordinator),
      object: staging.object,
      phase
    };
  }
}

class LocalTargetPort implements ServiceRelocationTargetObjectPort<LocalHidden> {
  constructor(
    private readonly options: ZLinkHostRelocationOptions,
    private readonly meshName: string,
    private readonly envelope: ServiceRelocationEnvelope
  ) {}

  async createHidden(
    participant: ServiceRelocationParticipant,
    signal?: AbortSignal
  ): Promise<LocalHidden> {
    const identity = decodeAuthorityKey({ value: participant.key } as ZLinkAuthorityKey).globalId;
    if (participant.objectKind === 'actor') {
      const membership = this.envelope.memberships.find(value => value.actorKey === participant.key);
      if (membership === undefined) throw new Error(`Actor '${identity}' relocation membership is missing.`);
      const spotIdentity = decodeAuthorityKey({ value: membership.spotKey } as ZLinkAuthorityKey).globalId;
      const actor = await this.requireActorManager().prepareRelocationActor(
        identity,
        participant.stableType,
        participant.objectGeneration,
        participant.authorityOwnerGeneration,
        spotIdentity as RoutingId,
        membership.spotObjectGeneration,
        membership.membershipEpoch,
        signal
      );
      return {
        authorityKey: participant.key,
        participant,
        actor,
        initialized: false,
        restoredTimers: [],
        replayResults: [],
        replayPackets: [],
        terminalDeliveries: new Map()
      };
    }
    const registration = this.registration(participant.objectKind, participant.stableType);
    const activation = await this.requireSpotManager().prepareRelocationSpot(
      this.meshName,
      participant.objectKind,
      participant.stableType,
      registration.implementation as Type<ZLinkSpot | ZLinkInstanceSpot>,
      identity as RoutingId,
      participant.objectGeneration,
      participant.authorityOwnerGeneration,
      signal
    );
    return {
      authorityKey: participant.key,
      participant,
      activation,
      initialized: false,
      restoredTimers: [],
      replayResults: [],
      replayPackets: [],
      terminalDeliveries: new Map()
    };
  }

  async restoreApplicationState(
    hidden: LocalHidden,
    payload: Uint8Array,
    signal?: AbortSignal
  ): Promise<void> {
    const registration = this.registration(hidden.participant.objectKind, hidden.participant.stableType);
    if (registration.relocation.kind === 'recreate') {
      if (payload.byteLength !== 0) throw new Error('Recreate relocation contains snapshot state.');
      return;
    }
    if (registration.relocation.kind !== 'snapshot') {
      throw new Error(`Relocation is disabled for '${hidden.participant.stableType}'.`);
    }
    const adapter = await createProviderInstance(
      registration.relocation.adapterType,
      this.options.providerResolver
    ) as ZLinkActorRelocationAdapter<ZLinkActor> | ZLinkSpotRelocationAdapter<ZLinkSpot | ZLinkInstanceSpot>;
    await adapter.restore(
      (hidden.actor ?? hidden.activation?.spot) as never,
      payload,
      signal ?? new AbortController().signal
    );
  }

  async restoreMemberships(
    hidden: ReadonlyMap<string, LocalHidden>,
    memberships: readonly ServiceRelocationMembership[]
  ): Promise<void> {
    for (const membership of memberships) {
      const actor = hidden.get(membership.actorKey)?.actor;
      const spot = hidden.get(membership.spotKey)?.activation;
      if (actor === undefined) throw new Error('Relocation membership Actor staging is missing.');
      const state = this.requireActorManager().getState(actor.context.actorId)!;
      const spotId = decodeAuthorityKey({ value: membership.spotKey } as ZLinkAuthorityKey).globalId as RoutingId;
      state.setJoinedSpot(spotId, spot?.spot, membership.membershipEpoch);
      spot?.commitActorJoin(actor);
    }
  }

  async publish(hidden: LocalHidden): Promise<void> {
    if (hidden.actor !== undefined) {
      const store = this.requireLocationStore();
      const current = await requireAuthority(
        store,
        { value: hidden.authorityKey } as ZLinkAuthorityKey
      );
      const state = this.requireActorManager().getState(hidden.actor.context.actorId)!;
      state.setLocationGeneration(current.authorityOwnerGeneration);
      state.setOwnerLeaseGeneration(current.ownerLeaseGeneration);
      hidden.targetAuthorityOwnerGeneration = current.authorityOwnerGeneration;
      this.requireActorManager().publishRelocationActor(hidden.actor.context.actorId);
    } else if (hidden.activation !== undefined) {
      await this.requireSpotManager().publishRelocationSpot(hidden.activation);
    }
    hidden.initialized = true;
  }

  async replayAcceptedJournal(hidden: LocalHidden, payload: Uint8Array): Promise<void> {
    if (hidden.actor === undefined || payload.byteLength === 0) return;
    const target = decodeActorSession(payload);
    const state = this.requireActorManager().getState(hidden.actor.context.actorId)!;
    state.setRemoteBoundSessionTarget(target);
    state.setBoundSessionTransferTarget(target);
    if (target.bindingGeneration !== undefined) {
      state.setBoundSessionBindingGeneration(target.bindingGeneration);
    }
  }

  async replayQueuedMessage(
    hidden: LocalHidden,
    message: ServiceRelocationQueuedMessage
  ): Promise<void> {
    if (hidden.actor === undefined) {
      throw new Error('Only Actor relocation participants can contain packet backlog.');
    }
    const packet = decodeQueuedHandoffPacket(message);
    const state = this.requireActorManager().getState(hidden.actor.context.actorId);
    if (state?.spotId === undefined) throw new Error('Relocated Actor membership is not staged.');
    const [result] = await replayActorHandoffBacklog(
      [packet],
      (parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef) =>
        this.requireSpotManager().dispatchRoutedActorPacket(
          state.spotId!,
          hidden.actor!.context.actorId,
          parts,
          returnResponse,
          remoteBoundSessionTarget,
          fallbackActorRef
        )
    );
    hidden.replayResults.push(result!);
    hidden.replayPackets.push(packet);
  }

  async restoreTimer(hidden: LocalHidden, timer: ServiceRelocationTimer): Promise<void> {
    if (hidden.activation === undefined) throw new Error('Actor relocation cannot restore a Spot timer.');
    hidden.restoredTimers.push(timer);
    if (hidden.restoredTimers.length !== hidden.participant.timers.length) return;
    hidden.activation.timers.restoreRelocation(hidden.restoredTimers.map(value => ({
      name: value.timerId,
      periodMs: value.intervalMs,
      overrunPolicy: value.overrunPolicy as never,
      maxCatchUpTicks: value.maxCatchUpTicks,
      stopOnUnhandledException: value.stopOnUnhandledException,
      startedAtUnixMs: value.startedAtUnixMs,
      deliveryIndex: value.deliveryIndex,
      lastScheduledIndex: value.lastScheduledIndex,
      pendingTicks: value.pendingTicks
    })));
  }

  async normalize(_hidden: LocalHidden): Promise<void> {}

  async openAdmission(hidden: LocalHidden): Promise<void> {
    if (hidden.actor !== undefined) {
      await this.options.actorTransfer.openRoutedActorSession(hidden.actor);
    }
  }

  async abort(hidden: LocalHidden): Promise<void> {
    if (hidden.actor !== undefined) {
      this.requireActorManager().abortRelocationActor(hidden.actor.context.actorId);
    } else if (hidden.activation !== undefined) {
      await this.requireSpotManager().abortRelocationSpot(hidden.activation);
    }
  }

  private registration(kind: ServiceRelocationParticipant['objectKind'], stableType: string): any {
    const node = this.options.registration.spotNodes.get(this.meshName);
    const value = kind === 'actor'
      ? node?.actorFactoryRegistrations?.[stableType]
      : kind === 'user_spot'
        ? node?.spotFactoryRegistrations?.[stableType]
        : node?.instanceSpotFactoryRegistrations?.[stableType];
    if (value === undefined) throw new Error(`Relocation type '${stableType}' is not registered.`);
    return value;
  }

  private requireLocationStore() {
    const store = this.options.locationStore();
    if (store === undefined) throw new Error('Relocation target Location Store is unavailable.');
    return store;
  }

  private requireSpotManager(): DefaultZLinkSpotManager {
    const manager = this.options.spotManager();
    if (manager === undefined) throw new Error('Relocation target Spot manager is unavailable.');
    return manager;
  }

  private requireActorManager(): DefaultZLinkActorManager {
    const manager = this.options.actorManager();
    if (manager === undefined) throw new Error('Relocation target Actor manager is unavailable.');
    return manager;
  }
}

function relocationStorePort(store: ZLinkRelocationStore): ServiceRelocationStorePort {
  return {
    async put(payload, retentionMs, signal) {
      const result = await store.putRelocation(payload, retentionMs, signal);
      return {
        reference: result.reference.value,
        checksumCrc32c: result.checksumCrc32c,
        expiresAtMs: result.expiresAt.getTime(),
        storeNowMs: result.storeNow.getTime()
      };
    },
    get(reference, signal) {
      return store.getRelocation({ value: reference } as ZLinkRelocationReference, signal);
    },
    delete(reference, signal) {
      return store.deleteRelocation({ value: reference } as ZLinkRelocationReference, signal);
    }
  };
}

async function requireAuthority(
  store: Pick<ZLinkLocationStore, 'readAuthority'>,
  key: ZLinkAuthorityKey,
  signal?: AbortSignal
): Promise<ZLinkAuthoritySnapshot> {
  const current = await store.readAuthority(key, signal);
  if (current.kind !== 'snapshot') throw new Error(`Authority '${key.value}' is missing.`);
  return current;
}

function primaryKey(envelope: ServiceRelocationEnvelope): string {
  return envelope.participants.find(value => value.objectKind !== 'actor')?.key
    ?? envelope.participants[0]!.key;
}

function isUserSpotAggregate(envelope: ServiceRelocationEnvelope): boolean {
  return envelope.participants.some(value => value.objectKind === 'user_spot');
}

function relocationPhaseRank(phase: LocalStage['phase']): number {
  return ['prepared', 'published', 'replayed', 'sealed', 'routed', 'normalized', 'open'].indexOf(phase);
}

function requireRelocationPhase(stage: LocalStage, minimum: LocalStage['phase']): void {
  if (relocationPhaseRank(stage.phase) < relocationPhaseRank(minimum)) {
    throw new Error(`Relocation staging requires '${minimum}' before continuing.`);
  }
}

interface RelocationTerminalCompletion {
  readonly index: number;
  readonly operationId: string;
  readonly source: NonNullable<ZLinkActorHandoffPacket['source']>;
  readonly result: ZLinkActorHandoffResult;
  readonly delivery: 'pending' | 'terminalReceived' | 'alreadyTerminal' | 'sourceLeaseExpired';
}

type RelocationTerminalDelivery = Exclude<RelocationTerminalCompletion['delivery'], 'pending'>;

interface DurableRelocationTerminalCompletion extends RelocationTerminalCompletion {
  readonly participantKey: string;
  readonly participantId: bigint;
}

function localSuccessorProgress(stage: LocalStage): ServiceRelocationSuccessorProgress {
  const progress = new Map<string, import('../foundation/service-relocation-runtime').ServiceRelocationParticipantProgress>();
  for (const participant of stage.staging.envelope.participants) {
    const hidden = stage.staging.hidden.get(participant.key);
    const completions: RelocationTerminalCompletion[] = [];
    if (hidden?.actor !== undefined) {
      for (let index = 0; index < hidden.replayPackets.length; index++) {
        const packet = hidden.replayPackets[index]!;
        if (!packet.returnResponse || packet.source === undefined) continue;
        completions.push({ index: packet.index, operationId: packet.operationId,
          source: packet.source, result: hidden.replayResults[index]!,
          delivery: hidden.terminalDeliveries.get(packet.index) ?? 'pending' });
      }
    }
    progress.set(participant.key, {
      replayCursor: participant.queuedMessages.at(-1)?.sequence ?? participant.replayCursor,
      terminalReplies: encodeTerminalCompletions(completions),
      pendingRelayCount: completions.filter(value => value.delivery === 'pending').length
    });
  }
  return progress;
}

function encodeTerminalCompletions(values: readonly RelocationTerminalCompletion[]): Buffer {
  if (values.length === 0) return Buffer.alloc(0);
  return Buffer.from(JSON.stringify(values, (_key, value) =>
    typeof value === 'bigint' ? value.toString() : value), 'utf8');
}

function decodeTerminalCompletions(payload: Uint8Array): readonly RelocationTerminalCompletion[] {
  if (payload.byteLength === 0) return [];
  const parsed: unknown = JSON.parse(Buffer.from(payload).toString('utf8'));
  if (!Array.isArray(parsed)) throw new TypeError('Relocation terminal completion list is invalid.');
  return parsed.map((item, position) => {
    if (!isRecord(item) || !Number.isSafeInteger(item.index) || (item.index as number) < 0
      || typeof item.operationId !== 'string' || !/^\d+:\d+$/.test(item.operationId)
      || !isRecord(item.source) || typeof item.source.ownerId !== 'string'
      || typeof item.source.ownerLeaseGeneration !== 'string'
      || typeof item.source.nodeRid !== 'string' || typeof item.source.nodeGeneration !== 'string'
      || typeof item.source.replyRouteId !== 'string'
      || !isRecord(item.result) || item.result.index !== item.index || typeof item.result.ok !== 'boolean'
      || !['pending', 'terminalReceived', 'alreadyTerminal', 'sourceLeaseExpired'].includes(
        item.delivery as string
      )) {
      throw new TypeError(`Relocation terminal completion ${position} is invalid.`);
    }
    const source = {
      ownerId: item.source.ownerId,
      ownerLeaseGeneration: positiveDecimal(item.source.ownerLeaseGeneration, 'source owner lease'),
      nodeRid: item.source.nodeRid,
      nodeGeneration: positiveDecimal(item.source.nodeGeneration, 'source node generation'),
      replyRouteId: positiveDecimal(item.source.replyRouteId, 'reply route')
    };
    operationWireId(item.operationId);
    return {
      index: item.index as number,
      operationId: item.operationId,
      source,
      result: item.result as unknown as ZLinkActorHandoffResult,
      delivery: item.delivery as RelocationTerminalCompletion['delivery']
    };
  });
}

function decodeQueuedHandoffPacket(message: ServiceRelocationQueuedMessage): ZLinkActorHandoffPacket {
  const value = JSON.parse(Buffer.from(message.payload).toString('utf8')) as Record<string, unknown>;
  const index = value.index;
  if (!Number.isSafeInteger(index) || index as number < 0) {
    throw new TypeError('Relocation Actor packet index is invalid.');
  }
  const decoded = decodeHandoffBacklog([{ ...value, index: 0 }])[0]!;
  return { ...decoded, index: index as number };
}

function relocationWireId(value: string): { readonly high: bigint; readonly low: bigint } {
  const hex = value.replaceAll('-', '');
  if (!/^[0-9a-fA-F]{32}$/.test(hex)) {
    throw new Error(`Relocation aggregate '${value}' is not a canonical 128-bit identity.`);
  }
  const result = {
    high: BigInt(`0x${hex.slice(0, 16)}`),
    low: BigInt(`0x${hex.slice(16)}`)
  };
  if (result.high === 0n && result.low === 0n) {
    throw new Error('Relocation identity must not be zero.');
  }
  return result;
}

function operationWireId(value: string): { readonly high: bigint; readonly low: bigint } {
  const match = /^(\d+):(\d+)$/.exec(value);
  if (match === null) throw new Error(`Actor handoff operation '${value}' is not canonical.`);
  const result = { high: BigInt(match[1]!), low: BigInt(match[2]!) };
  if (result.high === 0n && result.low === 0n) {
    throw new Error('Actor handoff operation identity must not be zero.');
  }
  return result;
}

function sameWireId(
  left: { readonly high: bigint; readonly low: bigint },
  right: { readonly high: bigint; readonly low: bigint }
): boolean {
  return left.high === right.high && left.low === right.low;
}

function sameCoordinator(
  left: ServiceWireRelocationCoordinatorFence,
  right: ServiceWireRelocationCoordinatorFence
): boolean {
  return left.ownerId === right.ownerId
    && left.leaseGeneration === right.leaseGeneration
    && left.nodeRid === right.nodeRid
    && left.nodeGeneration === right.nodeGeneration
    && left.expectedAuthorityStoreVersion === right.expectedAuthorityStoreVersion;
}

function sameRequestSource(
  left: ServiceWireRequestSourceFence,
  right: ServiceWireRequestSourceFence
): boolean {
  return left.ownerId === right.ownerId
    && left.leaseGeneration === right.leaseGeneration
    && left.nodeRid === right.nodeRid
    && left.nodeGeneration === right.nodeGeneration;
}

function sameHandoffSource(
  left: NonNullable<ZLinkActorHandoffPacket['source']>,
  right: NonNullable<ZLinkActorHandoffPacket['source']>
): boolean {
  return left.ownerId === right.ownerId
    && left.ownerLeaseGeneration === right.ownerLeaseGeneration
    && left.nodeRid === right.nodeRid
    && left.nodeGeneration === right.nodeGeneration
    && left.replyRouteId === right.replyRouteId;
}

function handoffSourceFence(
  value: NonNullable<ZLinkActorHandoffPacket['source']>
): ServiceWireRequestSourceFence {
  return {
    ownerId: value.ownerId,
    leaseGeneration: BigInt(value.ownerLeaseGeneration),
    nodeRid: value.nodeRid,
    nodeGeneration: BigInt(value.nodeGeneration)
  };
}

function maintenanceReplyRelay(
  stage: LocalStage,
  coordinator: ServiceWireRelocationCoordinatorFence,
  participantId: bigint,
  completion: RelocationTerminalCompletion
): ServiceMaintenanceReplyRelay {
  const value = completion.result.ok ? completion.result.response : undefined;
  return {
    relocation: relocationWireId(stage.staging.envelope.aggregateId),
    targetAttemptGeneration: stage.staging.envelope.aggregateGeneration,
    coordinator,
    operation: operationWireId(completion.operationId),
    replyRouteId: BigInt(completion.source.replyRouteId),
    participantId,
    sequence: BigInt(completion.index + 1),
    terminalResult: completion.result.ok ? 0 : 105,
    failureCode: completion.result.ok ? 0 : 17,
    ...(value === undefined ? {} : { payload: {
      packetName: 'zlink.relocation.reply',
      contentType: 'application/json',
      bytes: Buffer.from(JSON.stringify(value), 'utf8')
    } })
  };
}

function handoffResultFromRelay(relay: ServiceMaintenanceReplyRelay): ZLinkActorHandoffResult {
  if (relay.sequence > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new TypeError('Relocation terminal sequence exceeds the Node index range.');
  }
  let value: unknown;
  if (relay.payload !== undefined) {
    if (relay.terminalResult !== 0
      || relay.payload.packetName !== 'zlink.relocation.reply'
      || relay.payload.contentType !== 'application/json') {
      throw new TypeError('Relocation terminal payload boundary is invalid.');
    }
    value = JSON.parse(Buffer.from(relay.payload.bytes).toString('utf8')) as unknown;
  }
  const index = Number(relay.sequence - 1n);
  return relay.terminalResult === 0
    ? { index, ok: true, ...(value === undefined ? {} : { response: value }) }
    : { index, ok: false, error: `Actor handoff failed with framework code ${relay.failureCode}.` };
}

function wireIdText(value: { readonly high: bigint; readonly low: bigint }): string {
  return `${value.high}:${value.low}`;
}

function relocationStagingId(value: {
  readonly relocation: { readonly high: bigint; readonly low: bigint };
  readonly targetAttemptGeneration: bigint;
}): string {
  return `${value.relocation.high}:${value.relocation.low}:${value.targetAttemptGeneration}`;
}

function coordinatorSource(value: ServiceWireRelocationCoordinatorFence) {
  return { ownerId: value.ownerId, leaseGeneration: value.leaseGeneration,
    nodeRid: value.nodeRid, nodeGeneration: value.nodeGeneration };
}

function relocationObject(envelope: ServiceRelocationEnvelope): ServiceWireRelocationObject {
  const participant = envelope.participants.find(value => value.objectKind !== 'actor')
    ?? envelope.participants[0]!;
  const identity = decodeAuthorityKey({ value: participant.key } as ZLinkAuthorityKey).globalId;
  if (participant.objectKind === 'actor') {
    return { kind: 'actor', actorId: identity, objectGeneration: participant.objectGeneration,
      expectedAuthorityOwnerGeneration: participant.authorityOwnerGeneration };
  }
  if (participant.objectKind === 'user_spot') {
    return { kind: 'userSpot', spotId: identity, objectGeneration: participant.objectGeneration,
      expectedAuthorityOwnerGeneration: participant.authorityOwnerGeneration };
  }
  return { kind: 'instanceSpot', stableType: participant.stableType,
    spotId: identity, objectGeneration: participant.objectGeneration };
}

function relocationParticipants(
  envelope: ServiceRelocationEnvelope,
  coordinator: ServiceWireRelocationCoordinatorFence,
  candidate: ServiceWireRelocationCandidate
): readonly ServiceWireRelocationParticipant[] {
  return envelope.participants.map((participant, index) => ({
    participantId: BigInt(index + 1),
    allowanceMessages: BigInt(participant.queuedMessages.length),
    allowanceBytes: participant.queuedMessages.reduce(
      (sum, message) => sum + BigInt(canonicalQueuedFrozenRecord(
        participant,
        message,
        coordinator,
        candidate
      ).canonicalBytes.byteLength), 0n)
  }));
}

function canonicalQueuedFrozenRecord(
  participant: ServiceRelocationParticipant,
  message: ServiceRelocationQueuedMessage,
  coordinator: ServiceWireRelocationCoordinatorFence,
  candidate: ServiceWireRelocationCandidate
) {
  if (participant.objectKind !== 'actor') {
    throw new Error('Only Actor object-mailbox participants can contain accepted packets.');
  }
  const packet = decodeQueuedHandoffPacket(message);
  const identity = decodeAuthorityKey({ value: participant.key } as ZLinkAuthorityKey).globalId;
  const source = packet.source === undefined
    ? coordinatorSource(coordinator)
    : {
        ownerId: packet.source.ownerId,
        leaseGeneration: BigInt(packet.source.ownerLeaseGeneration),
        nodeRid: packet.source.nodeRid,
        nodeGeneration: BigInt(packet.source.nodeGeneration)
      };
  const operationId = parseHandoffOperationId(packet.operationId);
  if (packet.returnResponse && packet.source === undefined) {
    throw new Error('Captured Actor request has no exact request-source fence.');
  }
  return encodeServiceWireFrozenActorApplicationRecord({
    source,
    target: {
      actorId: identity,
      objectGeneration: participant.objectGeneration,
      nodeRid: candidate.nodeRid,
      nodeGeneration: candidate.nodeGeneration,
      authorityOwnerGeneration: participant.authorityOwnerGeneration + 1n,
      ownerLeaseGeneration: candidate.ownerLeaseGeneration
    },
    operationId,
    ...(packet.returnResponse ? { replyRouteId: BigInt(packet.source!.replyRouteId) } : {}),
    payload: {
      packetName: '__zlink.actor.handoff.accepted',
      contentType: 'application/json',
      bytes: message.payload
    }
  });
}

function parseHandoffOperationId(value: string): { readonly high: bigint; readonly low: bigint } {
  const parts = value.split(':');
  if (parts.length !== 2) throw new Error('Captured Actor operation ID is invalid.');
  const operation = { high: BigInt(parts[0]!), low: BigInt(parts[1]!) };
  if (operation.high === 0n && operation.low === 0n) {
    throw new Error('Captured Actor operation ID must not be zero.');
  }
  return operation;
}

function relocationCapacityOffer(
  request: ServiceMaintenanceRelocationPrepare,
  envelope: ServiceRelocationEnvelope,
  offeredMessages: bigint,
  offeredBytes: bigint
): Extract<ServiceMaintenanceRelocationControl, { kind: 'ready' }> {
  return { kind: 'ready', relocation: request.relocation,
    targetAttemptGeneration: request.targetAttemptGeneration, round: request.round,
    coordinator: request.coordinator, candidate: request.candidate, object: request.object,
    role: 'target', offeredMessages, offeredBytes,
    participants: [], sourceNodeGeneration: request.sourceNodeGeneration,
    targetNodeGeneration: request.candidate.nodeGeneration,
    reservationGeneration: request.targetAttemptGeneration, root: request.root,
    applicationVersion: request.applicationVersion,
    participantProgress: envelope.participants.map((participant, index) => ({
      participantId: BigInt(index + 1),
      acceptedBoundary: participant.queuedMessages.at(-1)?.sequence ?? participant.replayCursor,
      replayCursor: participant.replayCursor
    })) };
}

function validateControlEnvelope(
  request: ServiceMaintenanceRelocationPrepare,
  envelope: ServiceRelocationEnvelope
): void {
  if (!sameWireId(request.relocation, relocationWireId(envelope.aggregateId))
    || request.targetAttemptGeneration !== envelope.aggregateGeneration) {
    throw new Error('Relocation prepare durable envelope identity changed.');
  }
  const expectedParticipants = relocationParticipants(
    envelope,
    request.coordinator,
    request.candidate
  );
  if (stringifyWire(request.participants) !== stringifyWire(expectedParticipants)) {
    throw new Error(
      `Relocation prepare participant inventory changed: expected ${stringifyWire(expectedParticipants)}, received ${stringifyWire(request.participants)}.`
    );
  }
}

function validatePrepareOfferRetry(
  offer: TargetRelocationOffer,
  request: ServiceMaintenanceRelocationPrepare
): void {
  if (offer.prepareFingerprint !== stringifyWire(request)) {
    throw new Error('Relocation prepare retry changed its durable root or offer fence.');
  }
}

function validateReadyAcceptance(
  offer: TargetRelocationOffer,
  request: Extract<ServiceMaintenanceRelocationControl, { kind: 'ready' }>
): void {
  const participants = relocationParticipants(
    offer.envelope,
    offer.prepare.coordinator,
    offer.prepare.candidate
  );
  const targetOffer = relocationCapacityOffer(
    offer.prepare,
    offer.envelope,
    offer.offeredMessages,
    offer.offeredBytes
  );
  const expected = {
    ...targetOffer,
    role: 'source' as const,
    offeredMessages: 0n,
    offeredBytes: 0n,
    participants
  };
  if (stringifyWire(request) !== stringifyWire(expected)
    || participants.reduce((sum, value) => sum + value.allowanceMessages, 0n)
      > offer.offeredMessages
    || participants.reduce((sum, value) => sum + value.allowanceBytes, 0n)
      > offer.offeredBytes) {
    throw new Error('Relocation source acceptance changed the exact target offer.');
  }
}

function validateStagingControl(
  offer: TargetRelocationOffer,
  request: ServiceMaintenanceRelocationControlData
): void {
  if (!sameCoordinator(offer.prepare.coordinator, request.coordinator)
    || stringifyWire(offer.prepare.object) !== stringifyWire(request.object)
    || request.source.nodeRid !== offer.authenticatedSourceNodeRid
    || request.source.nodeGeneration !== offer.prepare.sourceNodeGeneration) {
    throw new Error('Relocation staging control changed its prepare fence.');
  }
}

function relocationControlAck(
  request: ServiceMaintenanceRelocationControlData
): Extract<ServiceMaintenanceRelocationControl, { kind: 'ack' }> {
  return {
    kind: 'ack',
    relocation: request.relocation,
    targetAttemptGeneration: request.targetAttemptGeneration,
    coordinator: request.coordinator,
    senderRole: 'target',
    participantId: request.participantId,
    highWater: request.sequence
  };
}

function stringifyWire(value: unknown): string {
  return JSON.stringify(value, (_key, item) => typeof item === 'bigint' ? item.toString() : item);
}

function validateControlResponse(
  request: ZLinkServiceRelocationControlRequest,
  response: ZLinkServiceRelocationControlResponse
): void {
  const expected = request.kind === 'prepare' ? 'ready'
    : request.kind === 'ready' ? 'reserved'
      : request.kind === 'data' ? 'ack' : request.kind;
  if (response.kind !== expected
    || !sameWireId(response.relocation, request.relocation)
    || response.targetAttemptGeneration !== request.targetAttemptGeneration
    || !sameCoordinator(response.coordinator, request.coordinator)) {
    throw new Error('Relocation control response does not match the request fence.');
  }
}

function controlAckKey(request: ZLinkServiceRelocationControlRequest): string {
  const base = `relocation:${request.relocation.high}:${request.relocation.low}`;
  const attempt = `${base}:${request.targetAttemptGeneration}`;
  if (request.kind === 'prepare') return `${attempt}:ready`;
  if (request.kind === 'ready') return `${attempt}:reserved`;
  if (request.kind === 'data' || request.kind === 'ack') {
    return `${attempt}:ack:${request.participantId}`;
  }
  if (request.kind === 'seal') return `${attempt}:seal`;
  if (request.kind === 'complete') return `${attempt}:complete:${request.sourceCleanupState}`;
  return `${attempt}:${request.kind}`;
}

function controlResponseKey(packet: ZLinkServiceRelocationControlRequest): string | undefined {
  if (packet.kind === 'ready' && packet.role === 'target') {
    return `relocation:${packet.relocation.high}:${packet.relocation.low}:${packet.targetAttemptGeneration}:ready`;
  }
  if (packet.kind === 'reserved' || packet.kind === 'ack') return controlAckKey(packet);
  if (packet.kind === 'seal' && packet.senderRole === 'target') return controlAckKey(packet);
  if (packet.kind === 'complete' && packet.senderRole === 'target') return controlAckKey(packet);
  return undefined;
}

function replyRelayIdentityKey(value: {
  readonly relocation: { readonly high: bigint; readonly low: bigint };
  readonly coordinator: ServiceWireRelocationCoordinatorFence;
  readonly operation: { readonly high: bigint; readonly low: bigint };
}): string {
  return JSON.stringify([
    value.operation.high.toString(),
    value.operation.low.toString(),
    value.relocation.high.toString(),
    value.relocation.low.toString(),
    value.coordinator.ownerId,
    value.coordinator.leaseGeneration.toString(),
    value.coordinator.nodeRid,
    value.coordinator.nodeGeneration.toString(),
    value.coordinator.expectedAuthorityStoreVersion
  ]);
}

function replyRelayPendingKey(
  ackTargetNodeRid: string,
  value: Parameters<typeof replyRelayIdentityKey>[0]
): string {
  return JSON.stringify([ackTargetNodeRid, replyRelayIdentityKey(value)]);
}

function isServiceWireCommand(payload: Uint8Array, command: number): boolean {
  return payload.byteLength >= 5
    && payload[0] === 0x5a && payload[1] === 0x4d && payload[2] === 1
    && payload[3] === command;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function positiveDecimal(value: string, name: string): string {
  if (!/^\d+$/.test(value) || BigInt(value) <= 0n) {
    throw new TypeError(`Relocation terminal ${name} is invalid.`);
  }
  return value;
}

function encodeHandoffQueuedMessages(
  backlog: readonly ZLinkActorHandoffPacket[]
): readonly ServiceRelocationQueuedMessage[] {
  return backlog.map(packet => ({
    sequence: BigInt(packet.index) + 1n,
    payload: Buffer.from(JSON.stringify(packet), 'utf8')
  }));
}

function toServiceTimer(value: import('../spots/spot-timer').ZLinkTimerRelocationState): ServiceRelocationTimer {
  return {
    timerId: value.name,
    startedAtUnixMs: value.startedAtUnixMs,
    dueAtUnixMs: value.startedAtUnixMs + Number(value.lastScheduledIndex + 1n) * value.periodMs,
    intervalMs: value.periodMs,
    deliveryIndex: value.deliveryIndex,
    lastScheduledIndex: value.lastScheduledIndex,
    overrunPolicy: value.overrunPolicy,
    maxCatchUpTicks: value.maxCatchUpTicks,
    stopOnUnhandledException: value.stopOnUnhandledException,
    pendingTicks: value.pendingTicks
  };
}

function encodeActorSession(target: ZLinkRemoteBoundSessionTarget | undefined): Buffer {
  if (target === undefined) return Buffer.alloc(0);
  return Buffer.from(JSON.stringify({
    version: 1,
    routerChannelId: target.routerChannelId,
    targetNodeRid: String(target.targetNodeRid),
    spotId: String(target.spotId),
    sessionNodeRid: target.sessionNodeRid === undefined ? undefined : String(target.sessionNodeRid),
    sessionRid: target.sessionRid === undefined ? undefined : String(target.sessionRid),
    bindingGeneration: target.bindingGeneration?.toString(),
    previousAuthorityOwnerGeneration: target.previousAuthorityOwnerGeneration?.toString(),
    previousOwnerLeaseGeneration: target.previousOwnerLeaseGeneration?.toString(),
    acceptedHighWater: target.acceptedHighWater?.toString(),
    relocationSealId: target.relocationSealId,
    acceptedJournalReference: target.acceptedJournalReference,
    acceptedJournalChecksumCrc32c: target.acceptedJournalChecksumCrc32c
  }), 'utf8');
}

function decodeActorSession(payload: Uint8Array): ZLinkRemoteBoundSessionTarget {
  const value = JSON.parse(Buffer.from(payload).toString('utf8')) as Record<string, unknown>;
  const optionalBigInt = (field: string) => typeof value[field] === 'string'
    ? BigInt(value[field] as string)
    : undefined;
  if (value.version !== 1 || typeof value.routerChannelId !== 'string'
    || typeof value.targetNodeRid !== 'string' || typeof value.spotId !== 'string') {
    throw new TypeError('Actor relocation Session journal is invalid.');
  }
  return {
    routerChannelId: value.routerChannelId,
    targetNodeRid: value.targetNodeRid as RoutingId,
    spotId: value.spotId as RoutingId,
    ...(typeof value.sessionNodeRid === 'string' ? { sessionNodeRid: value.sessionNodeRid as RoutingId } : {}),
    ...(typeof value.sessionRid === 'string' ? { sessionRid: value.sessionRid as RoutingId } : {}),
    ...(optionalBigInt('bindingGeneration') === undefined ? {} : { bindingGeneration: optionalBigInt('bindingGeneration') }),
    ...(optionalBigInt('previousAuthorityOwnerGeneration') === undefined ? {} : { previousAuthorityOwnerGeneration: optionalBigInt('previousAuthorityOwnerGeneration') }),
    ...(optionalBigInt('previousOwnerLeaseGeneration') === undefined ? {} : { previousOwnerLeaseGeneration: optionalBigInt('previousOwnerLeaseGeneration') }),
    ...(optionalBigInt('acceptedHighWater') === undefined ? {} : { acceptedHighWater: optionalBigInt('acceptedHighWater') }),
    ...(typeof value.relocationSealId === 'string' ? { relocationSealId: value.relocationSealId } : {}),
    ...(typeof value.acceptedJournalReference === 'string' ? { acceptedJournalReference: value.acceptedJournalReference } : {}),
    ...(typeof value.acceptedJournalChecksumCrc32c === 'number'
      ? { acceptedJournalChecksumCrc32c: value.acceptedJournalChecksumCrc32c }
      : {})
  };
}

function encodeMembershipMutation(
  memberships: readonly ServiceRelocationMembership[],
  participantKey: string
): Buffer {
  return Buffer.from(JSON.stringify(memberships.filter(value =>
    value.actorKey === participantKey || value.spotKey === participantKey).map(value => ({
      ...value,
      spotObjectGeneration: value.spotObjectGeneration.toString(),
      membershipEpoch: value.membershipEpoch.toString()
    }))), 'utf8');
}

function addCapacity(
  left: import('../../contracts').ZLinkCapacityVector,
  right: import('../../contracts').ZLinkCapacityVector
): import('../../contracts').ZLinkCapacityVector {
  const spotType = left.spotType ?? right.spotType;
  if (left.spotType !== undefined && right.spotType !== undefined
    && (left.spotType.objectKind !== right.spotType.objectKind
      || left.spotType.stableType !== right.spotType.stableType)) {
    throw new Error('Relocation aggregate contains more than one Spot stable type.');
  }
  return {
    actors: left.actors + right.actors,
    spots: left.spots + right.spots,
    ...(spotType === undefined ? {} : {
      spotType: {
        ...spotType,
        count: (left.spotType?.count ?? 0) + (right.spotType?.count ?? 0)
      }
    })
  };
}
