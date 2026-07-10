import type {
  ZLinkSpot,
  ZLinkMessage,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotContext
} from '@zlink-systems/framework';
import type {
  ActorPingRes,
  ActorPingReq,
  ActorPushNotify,
  ActorPushReq,
  LeaveReq,
  LeaveRes
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { SpotMsgHandler, SpotOutboundHandler, SpotOutboundNegativeHandler } from '../Handlers/spot-outbound-handlers';
import { SpotToSpotHandler, SpotToSpotNegativeHandler, SpotToSpotTimeoutHandler } from '../Handlers/spot-to-spot-handlers';
import { StageProbeHandler, StageTimerStartHandler } from '../Handlers/stage-handlers';
import { SlowSpotHandler, StateCommandHandler, StateReqHandler } from '../Handlers/state-req-handler';
import { ScenarioActor } from './scenario-actors';

export class ScenarioUserSpot implements ZLinkSpot {
  private static evidence?: EvidenceStore;
  readonly context!: ZLinkSpotContext;
  value = 0;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.packet('StateReq', StateReqHandler);
    this.context.handlers.packet('StateMsg', StateCommandHandler);
    this.context.handlers.packet('StageProbeReq', StageProbeHandler);
    this.context.handlers.packet('StageTimerStartMsg', StageTimerStartHandler);
    this.context.handlers.packet('SlowSpotReq', SlowSpotHandler);
    this.context.handlers.packet('SpotOutboundMsg', SpotOutboundHandler);
    this.context.handlers.packet('SpotOutboundNegativeMsg', SpotOutboundNegativeHandler);
    this.context.handlers.packet('SpotToSpotReq', SpotToSpotHandler);
    this.context.handlers.packet('SpotToSpotTimeoutReq', SpotToSpotTimeoutHandler);
    this.context.handlers.packet('SpotToSpotNegativeReq', SpotToSpotNegativeHandler);
    this.context.handlers.actorRequest('UserActorPingReq', UserActorPingHandler, ScenarioActor);
    this.context.handlers.actorRequest('UserActorPushReq', UserActorPushHandler, ScenarioActor);
    this.context.handlers.actorRequest('LeaveReq', UserActorLeaveHandler, ScenarioActor);
    this.context.handlers.subscribe(SpotServiceNames.spotEventTopic, SpotMsgHandler);
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

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
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

  async onJoinedActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-joined|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actorId}`
    );
  }

  async onLeaveActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-left|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actorId}`
    );
  }

  async onDisconnectActor(actor: ScenarioActor): Promise<void> {
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `spot-actor-disconnected|rid=${this.context.nodeRid}|spot=${this.context.spotRid}|actor=${actor.actorId}`
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
  implements ZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPingReq, ActorPingRes> {
  async handle(
    spot: ScenarioUserSpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingRes> {
    void context;
    actor.seen += 1;
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `actor-pingMsg|rid=${spot.context.nodeRid}|actor=${actor.actorId}`
      + `|spot=${spot.context.spotRid}|value=${request.value}|seen=${actor.seen}`
    );
    return {
      actorId: actor.actorId,
      nodeRid: String(spot.context.nodeRid),
      spotRid: String(spot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class UserActorPushHandler
  implements ZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPushReq, ActorPingRes> {
  async handle(
    spot: ScenarioUserSpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushReq
  ): Promise<ActorPingRes> {
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
      nodeRid: String(spot.context.nodeRid),
      spotRid: String(spot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class UserActorLeaveHandler
  implements ZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, LeaveReq, LeaveRes> {
  async handle(
    spot: ScenarioUserSpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: LeaveReq
  ): Promise<LeaveRes> {
    if (request.actorId !== actor.actorId) {
      throw new Error('Leave request actor does not match dispatched actor.');
    }
    await spot.context.leaveActor(actor, context.connectionAborted);
    return {
      actorId: actor.actorId,
      accepted: true
    };
  }
}

export class ScenarioAlternateSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
}
