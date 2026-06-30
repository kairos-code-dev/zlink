import { Inject, Injectable } from '@nestjs/common';
import type {
  ControlPingReply,
  ControlPingReq,
  CreateSpotReply,
  CreateSpotReq,
  EnsureActorReply,
  EnsureActorReq,
  JoinUserSpotActorReply,
  JoinUserSpotActorReq
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import type {
  ZLinkActorManager,
  ZLinkSpotManager,
  ZLinkRouteRequestContext,
  ZLinkRouteRequestHandler
} from '@zlink-systems/framework';
import { ZLINK_ACTOR_MANAGER, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemoryActorSpotStore } from '../Infrastructure/actor-spot-store';
import { InMemorySpotRouteStore } from '../Infrastructure/spot-route-store';
import { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
export class ControlPingHandler implements ZLinkRouteRequestHandler<ControlPingReq, ControlPingReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ControlPingReq, context: ZLinkRouteRequestContext): Promise<ControlPingReply> {
    void context;
    this.evidence.add(`control-ping|rid=${this.evidence.rid}|value=${request.value}`);
    return {
      value: request.value,
      nodeRid: this.evidence.rid
    };
  }
}

@Injectable()
export class EnsureActorHandler implements ZLinkRouteRequestHandler<EnsureActorReq, EnsureActorReply> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: EnsureActorReq, context: ZLinkRouteRequestContext): Promise<EnsureActorReply> {
    void context;
    const actorRef = await this.actors.getOrCreate(
      request.actorId,
      SpotServiceNames.actorType,
      request
    );
    this.evidence.add(`ensure-actor|rid=${this.evidence.rid}|actor=${request.actorId}`);
    this.evidence.add(`entry-joined|rid=${this.evidence.rid}|actor=${request.actorId}`);
    return {
      actorId: actorRef.actorId,
      nodeRid: String(actorRef.nodeRid),
      generation: actorRef.generation.toString()
    };
  }
}

@Injectable()
export class CreateSpotHandler implements ZLinkRouteRequestHandler<CreateSpotReq, CreateSpotReply> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: CreateSpotReq, context: ZLinkRouteRequestContext): Promise<CreateSpotReply> {
    void context;
    const created = await this.spots.getOrCreate(ScenarioUserSpot, request.spotRid);
    const state = typeof created.state === 'string' ? created.state : String(created.state);
    InMemorySpotRouteStore.recordUserSpot(String(created.spotRid), this.evidence.rid);
    this.evidence.add(`create-spot|rid=${this.evidence.rid}|spot=${created.spotRid}|state=${state}`);
    return {
      spotRid: String(created.spotRid),
      nodeRid: this.evidence.rid,
      state
    };
  }
}

@Injectable()
export class JoinUserSpotActorHandler implements ZLinkRouteRequestHandler<JoinUserSpotActorReq, JoinUserSpotActorReply> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: JoinUserSpotActorReq, context: ZLinkRouteRequestContext): Promise<JoinUserSpotActorReply> {
    void context;
    const actorRef = await this.actors.getOrCreate(
      request.actorId,
      SpotServiceNames.actorType,
      { displayName: request.spotRid }
    );
    InMemoryActorSpotStore.record(request.actorId, request.spotRid);
    this.evidence.add(
      `join-user-spot-actor|rid=${this.evidence.rid}|spot=${request.spotRid}`
      + `|actor=${request.actorId}|accepted=true`
    );
    this.evidence.add(`spot-actor-joined|rid=${this.evidence.rid}|spot=${request.spotRid}|actor=${request.actorId}`);
    return {
      spotRid: request.spotRid,
      actorId: actorRef.actorId,
      accepted: true,
      generation: actorRef.generation.toString()
    };
  }
}
