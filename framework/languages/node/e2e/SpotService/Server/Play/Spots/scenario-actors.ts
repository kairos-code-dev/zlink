import { Inject } from '@nestjs/common';
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
import { ActorPushNotify, SpotServiceNames } from '../../../Shared/messages';
import type {
  ZLinkActor,
  ZLinkActorClient,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import { ZLINK_ACTOR_CLIENT, zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemoryActorSpotStore } from '../Infrastructure/actor-spot-store';

export class ScenarioActor implements ZLinkActor {
  displayName: string;
  seen = 0;

  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {
    this.displayName = actorId;
  }

  configure(): void {
    this.context.handlers.addHandler(EntryActorPingHandler);
    this.context.handlers.addHandler(EntrySlowActorPingHandler);
    this.context.handlers.addHandler(ActorPushHandler);
    this.context.handlers.addHandler(EntryUserActorPingHandler);
    this.context.handlers.addHandler(EntryUserActorPushHandler);
    this.context.handlers.addHandler(ComplexActorHandler);
    this.context.handlers.addHandler(EntryUserSpotActorJoinHandler);
    this.context.handlers.addHandler(EntryActorLeaveHandler);
    this.context.handlers.addHandler(EntryActorSnapshotHandler);
    this.context.handlers.addHandler(InitializeScenarioActorHandler);
  }
}

class InitializeScenarioActor {
  constructor(readonly displayName: string) {}
}

export class InitializeScenarioActorHandler {
  @ZLinkSpotActorSend('InitializeScenarioActor')
  async handle(actor: ScenarioActor, _context: unknown, message: InitializeScenarioActor): Promise<void> {
    actor.displayName = message.displayName;
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

  constructor(@Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient) {}

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async onCreateActor(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void> {
    const request = createRequest.decode<Partial<EnsureActorReq>>(Object as never);
    if (typeof request.displayName === 'string') {
      await this.actors
        .sendToActor(SpotServiceNames.spotChannel, actor.actor, new InitializeScenarioActor(request.displayName))
        .submit();
    }
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-created|rid=${evidence.rid}|actor=${actor.actor.actorId}`);
  }

  async onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    return { accepted: true, reply: request.decode() };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-joined|rid=${evidence.rid}|actor=${actor.actor.actorId}`);
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-left|rid=${evidence.rid}|actor=${actor.actor.actorId}`);
  }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioEntrySpot.requireEvidence();
    evidence.add(`entry-disconnected|rid=${evidence.rid}|actor=${actor.actor.actorId}`);
  }

  scheduleDestroy(actor: ScenarioActor): void {
    const evidence = ScenarioEntrySpot.requireEvidence();
    void this.context.runIoWorker(async () => true).submit().then(async () => {
      try {
        await this.context.destroyActor(actor);
        evidence.add(`actor-destroyed|rid=${evidence.rid}|actor=${actor.actorId}`);
      } catch (error) {
        evidence.add(
          `actor-destroy-failed|rid=${evidence.rid}|actor=${actor.actorId}`
          + `|error=${error instanceof Error ? error.name : String(error)}`
        );
      }
    });
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
{
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('ActorPingReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntryActorPingHandler.requireEvidence();
    actor.seen += 1;
    evidence.add(
      `actor-pingMsg|rid=${evidence.rid}|actor=${actor.actorId}`
      + `|spot=${actor.context.spotRid ?? evidence.rid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: evidence.rid,
      spotRid: String(actor.context.spotRid ?? evidence.rid),
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
{
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('SlowActorPingReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SlowActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntrySlowActorPingHandler.requireEvidence();
    evidence.add(
      `actor-slow-ping-start|rid=${evidence.rid}|actor=${actor.actorId}`
      + `|spot=${actor.context.spotRid ?? evidence.rid}|value=${request.value}`
    );
    await new Promise((resolve) => setTimeout(resolve, Math.max(0, request.delayMs)));
    actor.seen += 1;
    evidence.add(
      `actor-slow-pingMsg|rid=${evidence.rid}|actor=${actor.actorId}`
      + `|spot=${actor.context.spotRid ?? evidence.rid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: evidence.rid,
      spotRid: String(actor.context.spotRid ?? evidence.rid),
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
{
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('UserActorPingReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    const evidence = EntryUserActorPingHandler.requireEvidence();
    actor.seen += 1;
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    evidence.add(
      `actor-pingMsg|rid=${evidence.rid}|actor=${actor.actorId}`
      + `|spot=${spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: evidence.rid,
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
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorPushReq')
  async handle(
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
      nodeRid: this.evidence.rid,
      spotRid: String(actor.context.spotRid ?? this.evidence.rid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class EntryUserActorPushHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('UserActorPushReq')
  async handle(
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
      nodeRid: this.evidence.rid,
      spotRid,
      value: request.value,
      seen: actor.seen
    };
  }
}

export class ComplexActorHandler
{
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('ComplexActorReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ComplexActorReq
  ): Promise<ComplexActorRes> {
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
{
  private static evidence?: EvidenceStore;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  @ZLinkSpotActorRequest('LeaveReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: LeaveReq
  ): Promise<LeaveRes> {
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Leave request actor does not match dispatched actor.');
    }
    const evidence = EntryActorLeaveHandler.requireEvidence();
    const spotRid = InMemoryActorSpotStore.find(actor.actorId) ?? actor.displayName;
    evidence.add(
      `spot-actor-left|rid=${evidence.rid}|spot=${spotRid}|actor=${actor.actorId}`
    );
    await actor.context.leaveSpot(context.connectionAborted);
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
{
  @ZLinkSpotActorRequest('JoinUserSpotActorReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinUserSpotActorReq
  ): Promise<JoinUserSpotActorRes> {
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
{
  @ZLinkSpotActorRequest('SnapshotReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SnapshotReq
  ): Promise<SnapshotRes> {
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

@zlinkEntrySpotActorRequestHandler({
  actor: () => ScenarioActor,
  entrySpot: () => ScenarioEntrySpot,
  packetName: 'DestroyActorReq'
})
export class EntryActorDestroyHandler
{
  async handle(
    spot: ScenarioEntrySpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: DestroyActorReq
  ): Promise<DestroyActorRes> {
    void context;
    if (request.actorId !== actor.actorId) {
      throw new Error('Destroy request actor does not match dispatched actor.');
    }
    spot.scheduleDestroy(actor);
    return {
      actorId: actor.actorId,
      destroyed: true
    };
  }
}
