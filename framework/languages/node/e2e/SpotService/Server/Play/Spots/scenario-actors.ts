import type {
  ActorPushNotify,
  ActorPushReq,
  ComplexActorReq,
  ComplexActorReply,
  ActorPingReq,
  ActorPingReply,
  DestroyActorReply,
  DestroyActorReq,
  EnsureActorReq,
  LeaveReply,
  LeaveReq,
  SlowActorPingReq,
  SnapshotReply,
  SnapshotReq
} from '../../../Shared/messages';
import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkEntrySpot,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemoryActorSpotStore } from '../Infrastructure/actor-spot-store';

export class ScenarioActor implements ZLinkActor {
  displayName: string;
  seen = 0;

  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {
    this.displayName = actorId;
  }
}

export class ScenarioActorFactory implements ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): ScenarioActor {
    return new ScenarioActor(actorId, context);
  }
}

export class ScenarioEntrySpot implements ZLinkEntrySpot<ScenarioActor> {
  private static evidence?: EvidenceStore;
  readonly context!: ZLinkEntrySpotContext<ScenarioActor>;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.actorRequest('ActorPingReq', EntryActorPingHandler, ScenarioActor);
    this.context.handlers.actorRequest('SlowActorPingReq', EntrySlowActorPingHandler, ScenarioActor);
    this.context.handlers.actorRequest('ActorPushReq', ActorPushHandler, ScenarioActor);
    this.context.handlers.actorRequest('UserActorPingReq', EntryUserActorPingHandler, ScenarioActor);
    this.context.handlers.actorRequest('UserActorPushReq', EntryUserActorPushHandler, ScenarioActor);
    this.context.handlers.actorRequest('ComplexActorReq', ComplexActorHandler, ScenarioActor);
    this.context.handlers.actorRequest('LeaveReq', EntryActorLeaveHandler, ScenarioActor);
    this.context.handlers.actorRequest('SnapshotReq', EntryActorSnapshotHandler, ScenarioActor);
    this.context.handlers.actorRequest('DestroyActorReq', EntryActorDestroyHandler, ScenarioActor);
  }

  async onCreateActor(actor: ScenarioActor, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsureActorReq>>(Object as never);
    if (typeof request.displayName === 'string') {
      actor.displayName = request.displayName;
    }
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-created|rid=${evidence.rid}|actor=${actor.actorId}`);
  }

  async onActorJoin(actor: ScenarioActor, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    return { accepted: true, reply: request.decode() };
  }

  async onJoinedActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-joined|rid=${evidence.rid}|actor=${actor.actorId}`);
  }

  async onLeaveActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-left|rid=${evidence.rid}|actor=${actor.actorId}`);
  }

  async onDisconnectActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-disconnected|rid=${evidence.rid}|actor=${actor.actorId}`);
  }

  addEvidence(entry: string): void {
    ScenarioEntrySpot.requireEvidence().add(entry);
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ScenarioEntrySpot evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class EntryActorPingHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingReply> {
    void context;
    const evidence = EntryActorPingHandler.requireEvidence();
    actor.seen += 1;
    evidence.add(
      `actor-ping|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
      + `|spot=${entrySpot.context.spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: String(entrySpot.context.nodeRid),
      spotRid: String(entrySpot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('EntryActorPingHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class EntrySlowActorPingHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SlowActorPingReq, ActorPingReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SlowActorPingReq
  ): Promise<ActorPingReply> {
    void context;
    await new Promise((resolve) => setTimeout(resolve, Math.max(0, request.delayMs)));
    const evidence = EntrySlowActorPingHandler.requireEvidence();
    actor.seen += 1;
    evidence.add(
      `actor-slow-ping|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
      + `|spot=${entrySpot.context.spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: String(entrySpot.context.nodeRid),
      spotRid: String(entrySpot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('EntrySlowActorPingHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class EntryUserActorPingHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingReply> {
    void context;
    const evidence = EntryUserActorPingHandler.requireEvidence();
    actor.seen += 1;
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    evidence.add(
      `actor-ping|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
      + `|spot=${spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: String(entrySpot.context.nodeRid),
      spotRid,
      value: request.value,
      seen: actor.seen
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('EntryUserActorPingHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class ActorPushHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingReply> {
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushReq
  ): Promise<ActorPingReply> {
    void context;
    actor.seen += 1;
    await actor.context.boundSession
      .send({
        actorId: actor.actorId,
        value: request.value,
        seen: actor.seen
      } satisfies ActorPushNotify)
      .packetName('ActorPushNotify')
      .submit();
    return {
      actorId: actor.actorId,
      nodeRid: String(entrySpot.context.nodeRid),
      spotRid: String(entrySpot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class EntryUserActorPushHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingReply> {
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushReq
  ): Promise<ActorPingReply> {
    void context;
    actor.seen += 1;
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    await actor.context.boundSession
      .send({
        actorId: actor.actorId,
        value: request.value,
        seen: actor.seen
      } satisfies ActorPushNotify)
      .packetName('ActorPushNotify')
      .submit();
    return {
      actorId: actor.actorId,
      nodeRid: String(entrySpot.context.nodeRid),
      spotRid,
      value: request.value,
      seen: actor.seen
    };
  }
}

export class ComplexActorHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ComplexActorReq, ComplexActorReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ComplexActorReq
  ): Promise<ComplexActorReply> {
    void entrySpot;
    void context;
    const evidence = ComplexActorHandler.requireEvidence();
    actor.displayName = request.displayName;
    const attrs = Object.entries(request.attributes)
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([key, value]) => `${key}:${value}`)
      .join(',');
    evidence.add(
      `actor-complex|rid=${evidence.rid}|actor=${actor.actorId}|name=${request.displayName}`
      + `|level=${request.level}|tags=${request.tags.join(',')}|attrs=${attrs}`
    );
    return {
      actorId: actor.actorId,
      displayName: request.displayName,
      level: request.level,
      tags: request.tags,
      attributes: request.attributes
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ComplexActorHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class EntryActorLeaveHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, LeaveReq, LeaveReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: LeaveReq
  ): Promise<LeaveReply> {
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Leave request actor does not match dispatched actor.');
    }
    void entrySpot;
    const evidence = EntryActorLeaveHandler.requireEvidence();
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    evidence.add(
      `spot-actor-left|rid=${entrySpot.context.nodeRid}|spot=${spotRid}|actor=${actor.actorId}`
    );
    return {
      actorId: actor.actorId,
      accepted: true
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('EntryActorLeaveHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class EntryActorSnapshotHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SnapshotReq, SnapshotReply> {
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SnapshotReq
  ): Promise<SnapshotReply> {
    void entrySpot;
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Snapshot request actor does not match dispatched actor.');
    }
    return {
      actorId: actor.actorId,
      seen: actor.seen
    };
  }
}

export class EntryActorDestroyHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, DestroyActorReq, DestroyActorReply> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: DestroyActorReq
  ): Promise<DestroyActorReply> {
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Destroy request actor does not match dispatched actor.');
    }
    const evidence = EntryActorDestroyHandler.requireEvidence();
    entrySpot.context.runWorker(() => true).onCompleted(
      async (_result, signal) => {
        try {
          await entrySpot.context.destroyActor(actor, signal);
          evidence.add(`actor-destroyed|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`);
        } catch (error) {
          evidence.add(
            `actor-destroy-failed|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
            + `|error=${error instanceof Error ? error.name : String(error)}`
          );
        }
      },
      async (error) => {
        evidence.add(
          `actor-destroy-failed|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
          + `|error=${error instanceof Error ? error.name : String(error)}`
        );
      }
    );
    return {
      actorId: actor.actorId,
      destroyed: true
    };
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('EntryActorDestroyHandler evidence store is not configured.');
    }
    return this.evidence;
  }
}
