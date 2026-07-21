import { Inject, Injectable } from '@nestjs/common';
import type {
  ControlPingRes,
  ControlPingReq,
  CrossRoleActorPushReq,
  CrossRoleActorPushRes,
  CreateSpotRes,
  CreateSpotReq,
  EnsureActorRes,
  EnsureActorReq,
  ActorPingRes
} from '../../../Shared/messages';
import { ActorPushReq, SpotServiceNames } from '../../../Shared/messages';
import type {
  ZLinkActorClient,
  ZLinkActorManager,
  ZLinkSpotManager,
  ZLinkRouteRequestContext,
  ZLinkRouteRequestHandler
} from '@zlink-systems/framework';
import { ZLINK_ACTOR_CLIENT, ZLINK_ACTOR_MANAGER, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { InMemorySpotRouteStore } from '../Infrastructure/spot-route-store';
import { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
export class ControlPingHandler implements ZLinkRouteRequestHandler<ControlPingReq, ControlPingRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: ControlPingReq, context: ZLinkRouteRequestContext): Promise<ControlPingRes> {
    void context;
    this.evidence.add(`control-pingMsg|rid=${this.evidence.rid}|value=${request.value}`);
    return {
      value: request.value,
      nodeRid: this.evidence.rid
    };
  }
}

@Injectable()
export class EnsureActorHandler implements ZLinkRouteRequestHandler<EnsureActorReq, EnsureActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: EnsureActorReq, context: ZLinkRouteRequestContext): Promise<EnsureActorRes> {
    void context;
    const actorRef = await this.actors.getOrCreate(
      SpotServiceNames.spotChannel,
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
export class CrossRoleActorPushHandler
  implements ZLinkRouteRequestHandler<CrossRoleActorPushReq, CrossRoleActorPushRes> {
  constructor(
    @Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(
    request: CrossRoleActorPushReq,
    context: ZLinkRouteRequestContext
  ): Promise<CrossRoleActorPushRes> {
    void context;
    const reply = await this.actors
      .requestToActor(SpotServiceNames.spotChannel, {
        actorId: request.actorId,
        nodeRid: request.nodeRid,
        generation: BigInt(request.generation)
      }, new ActorPushReq(request.value))
      .timeout(5000)
      .submit<ActorPingRes>();
    this.evidence.add(
      `cross-role-push|rid=${this.evidence.rid}|actor=${request.actorId}|value=${request.value}|seen=${reply.seen}`
    );
    return {
      actorId: reply.actorId,
      nodeRid: reply.nodeRid,
      value: reply.value,
      delivered: true
    };
  }
}

@Injectable()
export class CreateSpotHandler implements ZLinkRouteRequestHandler<CreateSpotReq, CreateSpotRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: CreateSpotReq, context: ZLinkRouteRequestContext): Promise<CreateSpotRes> {
    void context;
    const created = await this.spots.getOrCreate(
      SpotServiceNames.spotChannel,
      ScenarioUserSpot,
      request.spotRid
    );
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
