import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse
} from '@zlink-systems/framework';
import { EvidenceStore } from '../Infrastructure/evidence-store';

export class ScenarioActor implements ZLinkActor {
  displayName: string;

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

  async onCreateActor(actor: ZLinkActorMembership, createRequest: ZLinkMessage): Promise<void> {
    void createRequest;
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
    if (actor.actor.actorId.startsWith('actor-sm-d5-fail-')) {
      throw new Error('SM-D5 injected disconnect callback failure.');
    }
  }

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ScenarioEntrySpot evidence store is not configured.');
    }
    return this.evidence;
  }
}
