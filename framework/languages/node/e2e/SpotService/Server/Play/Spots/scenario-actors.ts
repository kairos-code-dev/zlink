import type {
  ActorPushReq,
  ComplexActorReq,
  ComplexActorRes,
  ActorPingReq,
  ActorPingRes,
  DestroyActorRes,
  DestroyActorReq,
  EnsureActorReq,
  JoinUserSpotActorReq,
  JoinUserSpotActorRes,
  LeaveRes,
  LeaveReq,
  SlowActorPingReq,
  SnapshotRes,
  SnapshotReq
} from '../../../Shared/messages';
import { ActorPushNotify } from '../../../Shared/messages';
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
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
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
  async create(actorId: string, context: ZLinkActorContext): Promise<ScenarioActor> {
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
    this.context.handlers.addActorPacket(EntryActorPingHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntrySlowActorPingHandler, ScenarioActor);
    this.context.handlers.addActorPacket(ActorPushHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryUserActorPingHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryUserActorPushHandler, ScenarioActor);
    this.context.handlers.addActorPacket(ComplexActorHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryUserSpotActorJoinHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryActorLeaveHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryActorSnapshotHandler, ScenarioActor);
    this.context.handlers.addActorPacket(EntryActorDestroyHandler, ScenarioActor);
  }

  async onCreateActor(actor: ScenarioActor, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsureActorReq>>(Object as never);
    if (typeof request.displayName === 'string') {
      actor.displayName = request.displayName;
    }
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-created|rid=${evidence.rid}|actor=${actor.actorId}`);
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    void actorId;
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('ActorPingReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntryActorPingHandler.requireEvidence();
    actor.seen += 1;
    evidence.add(
      `actor-pingMsg|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SlowActorPingReq, ActorPingRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('SlowActorPingReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SlowActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntrySlowActorPingHandler.requireEvidence();
    evidence.add(
      `actor-slow-ping-start|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
      + `|spot=${entrySpot.context.spotRid}|value=${request.value}`
    );
    await new Promise((resolve) => setTimeout(resolve, Math.max(0, request.delayMs)));
    actor.seen += 1;
    evidence.add(
      `actor-slow-pingMsg|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('UserActorPingReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntryUserActorPingHandler.requireEvidence();
    actor.seen += 1;
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    evidence.add(
      `actor-pingMsg|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingRes> {
  @ZLinkSpotActorRequest('ActorPushReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushReq
  ): Promise<ActorPingRes> {
    void context;
    actor.seen += 1;
    actor.context.boundSession
      .send(new ActorPushNotify(actor.actorId, request.value, actor.seen))
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingRes> {
  @ZLinkSpotActorRequest('UserActorPushReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushReq
  ): Promise<ActorPingRes> {
    void context;
    actor.seen += 1;
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    actor.context.boundSession
      .send(new ActorPushNotify(actor.actorId, request.value, actor.seen))
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ComplexActorReq, ComplexActorRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('ComplexActorReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ComplexActorReq
  ): Promise<ComplexActorRes> {
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, LeaveReq, LeaveRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('LeaveReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: LeaveReq
  ): Promise<LeaveRes> {
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

export class EntryUserSpotActorJoinHandler
  implements ZLinkEntrySpotActorRequestHandler<
    ScenarioEntrySpot,
    ScenarioActor,
    JoinUserSpotActorReq,
    JoinUserSpotActorRes
  > {
  @ZLinkSpotActorRequest('JoinUserSpotActorReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinUserSpotActorReq
  ): Promise<JoinUserSpotActorRes> {
    void entrySpot;
    if (request.actorId !== actor.actorId) {
      throw new Error('Join request actor does not match dispatched actor.');
    }
    const result = await actor.context
      .joinSpot(request.spotRid, request)
      .timeout(5000)
      .submit(context.connectionAborted);
    if (result.status === 'rejected') {
      return {
        spotRid: request.spotRid,
        actorId: actor.actorId,
        accepted: false,
        generation: '0'
      };
    }
    InMemoryActorSpotStore.record(actor.actorId, request.spotRid);
    return {
      spotRid: request.spotRid,
      actorId: actor.actorId,
      accepted: true,
      generation: result.actor.generation.toString()
    };
  }
}

export class EntryActorSnapshotHandler
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SnapshotReq, SnapshotRes> {
  @ZLinkSpotActorRequest('SnapshotReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SnapshotReq
  ): Promise<SnapshotRes> {
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
  implements ZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, DestroyActorReq, DestroyActorRes> {
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('DestroyActorReq')
  async handle(
    entrySpot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: DestroyActorReq
  ): Promise<DestroyActorRes> {
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Destroy request actor does not match dispatched actor.');
    }
    const evidence = EntryActorDestroyHandler.requireEvidence();
    void entrySpot.context.runIoWorker(async () => true).submit().then(async () => {
      try {
        await entrySpot.context.destroyActor(actor);
        evidence.add(`actor-destroyed|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`);
      } catch (error) {
        evidence.add(
          `actor-destroy-failed|rid=${entrySpot.context.nodeRid}|actor=${actor.actorId}`
          + `|error=${error instanceof Error ? error.name : String(error)}`
        );
      }
    });
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
