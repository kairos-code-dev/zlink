import type {
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkSpot,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import type {
  ActorPingRes,
  ActorPingReq,
  ActorPushReq,
  LeaveReq,
  LeaveRes
} from '../../../Shared/messages';
import { ActorPushNotify } from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { SpotMsgHandler, SpotOutboundHandler, SpotOutboundNegativeHandler } from '../Handlers/spot-outbound-handlers';
import { SpotToSpotHandler, SpotToSpotNegativeHandler, SpotToSpotTimeoutHandler } from '../Handlers/spot-to-spot-handlers';
import { StageProbeHandler, StageTimerStartHandler } from '../Handlers/stage-handlers';
import { SlowSpotHandler, StateCommandHandler, StateReqHandler } from '../Handlers/state-req-handler';
import { SpotAdminHandler } from '../Handlers/spot-admin-handler';
import { ScenarioActor } from './scenario-actors';

export class ScenarioUserSpot implements ZLinkSpot {
  private static evidence?: EvidenceStore;
  readonly context!: ZLinkSpotContext;
  value = 0;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.addPacket(StateReqHandler);
    this.context.handlers.addPacket(StateCommandHandler);
    this.context.handlers.addPacket(StageProbeHandler);
    this.context.handlers.addPacket(StageTimerStartHandler);
    this.context.handlers.addPacket(SlowSpotHandler);
    this.context.handlers.addPacket(SpotOutboundHandler);
    this.context.handlers.addPacket(SpotOutboundNegativeHandler);
    this.context.handlers.addPacket(SpotToSpotHandler);
    this.context.handlers.addPacket(SpotToSpotTimeoutHandler);
    this.context.handlers.addPacket(SpotToSpotNegativeHandler);
    this.context.handlers.addPacket(SpotAdminHandler);
    this.context.handlers.addSubscribe(
      SpotMsgHandler,
      SpotServiceNames.spotChannel,
      SpotServiceNames.spotEventTopic
    );
  }

  async onInitialize(): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(`spot-initialize|rid=${evidence.rid}|spot=${this.context.spotRid}`);
  }

  async onClosing(): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(`spot-closing|rid=${evidence.rid}|spot=${this.context.spotRid}`);
  }

  add(delta: number): number {
    this.value += delta;
    return this.value;
  }

  async onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const actorId = actor.actor.actorId;
    const payload = request.decode<Partial<{ readonly actorId: string }>>(Object as never);
    if (payload.actorId?.includes('reject') === true) {
      const evidence = ScenarioUserSpot.requireEvidence();
      evidence.add(
        `spot-actor-join-rejected|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actorId}`
      );
      return { accepted: false, reply: { accepted: false, actorId } };
    }
    return { accepted: true };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-joined|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actor.actorId}`
    );
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-left|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actor.actorId}`
    );
  }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-disconnected|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actor.actorId}`
    );
  }

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ScenarioUserSpot evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class UserActorPingHandler
  implements ZLinkSpotActorRequestHandler<ScenarioActor, ActorPingReq, ActorPingRes> {
  @ZLinkSpotActorRequest('UserActorPingReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    actor.seen += 1;
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `actor-pingMsg|rid=${evidence.rid}|actor=${actor.actorId}`
      + `|spot=${actor.context.spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: evidence.rid,
      spotRid: String(actor.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class UserActorPushHandler
  implements ZLinkSpotActorRequestHandler<ScenarioActor, ActorPushReq, ActorPingRes> {
  @ZLinkSpotActorRequest('UserActorPushReq')
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
    const evidence = ScenarioUserSpot.requireEvidence();
    return {
      actorId: actor.actorId,
      nodeRid: evidence.rid,
      spotRid: String(actor.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class UserActorLeaveHandler
  implements ZLinkSpotActorRequestHandler<ScenarioActor, LeaveReq, LeaveRes> {
  @ZLinkSpotActorRequest('LeaveReq')
  async handle(
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: LeaveReq
  ): Promise<LeaveRes> {
    if (request.actorId !== actor.actorId) {
      throw new Error('Leave request actor does not match dispatched actor.');
    }
    await actor.context.leaveSpot(context.connectionAborted);
    return {
      actorId: actor.actorId,
      accepted: true
    };
  }
}

export class ScenarioAlternateSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
  async onDisconnectActor(): Promise<void> {}
}
