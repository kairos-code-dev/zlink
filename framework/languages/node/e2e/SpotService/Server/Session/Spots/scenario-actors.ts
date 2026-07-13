import type { EnsureActorReq } from '../../../Shared/messages';
import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
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

  private static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ScenarioEntrySpot evidence store is not configured.');
    }
    return this.evidence;
  }
}
