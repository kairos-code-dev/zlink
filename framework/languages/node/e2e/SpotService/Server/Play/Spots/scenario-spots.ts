import type {
  ZLinkSpot,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotContext
} from '@zlink-systems/framework';
import type { ActorPingReply, ActorPingReq, ActorPushNotify, ActorPushReq } from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import { SpotEventHandler, SpotOutboundHandler, SpotOutboundNegativeHandler } from '../Handlers/spot-outbound-handlers';
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
    this.context.handlers.packet('StateCommand', StateCommandHandler);
    this.context.handlers.packet('StageProbeReq', StageProbeHandler);
    this.context.handlers.packet('StageTimerStartCommand', StageTimerStartHandler);
    this.context.handlers.packet('SlowSpotReq', SlowSpotHandler);
    this.context.handlers.packet('SpotOutboundReq', SpotOutboundHandler);
    this.context.handlers.packet('SpotOutboundNegativeReq', SpotOutboundNegativeHandler);
    this.context.handlers.packet('SpotToSpotReq', SpotToSpotHandler);
    this.context.handlers.packet('SpotToSpotTimeoutReq', SpotToSpotTimeoutHandler);
    this.context.handlers.packet('SpotToSpotNegativeReq', SpotToSpotNegativeHandler);
    this.context.handlers.actorRequest('UserActorPingReq', UserActorPingHandler, ScenarioActor);
    this.context.handlers.actorRequest('UserActorPushReq', UserActorPushHandler, ScenarioActor);
    this.context.handlers.subscribe(SpotServiceNames.spotEventTopic, SpotEventHandler);
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

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('ScenarioUserSpot evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class UserActorPingHandler
  implements ZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPingReq, ActorPingReply> {
  async handle(
    spot: ScenarioUserSpot,
    actor: ScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPingReq
  ): Promise<ActorPingReply> {
    void context;
    actor.seen += 1;
    const evidence = ScenarioUserSpot.requireEvidence();
    evidence.add(
      `actor-ping|rid=${spot.context.nodeRid}|actor=${actor.actorId}`
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
  implements ZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPushReq, ActorPingReply> {
  async handle(
    spot: ScenarioUserSpot,
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
      nodeRid: String(spot.context.nodeRid),
      spotRid: String(spot.context.spotRid),
      value: request.value,
      seen: actor.seen
    };
  }
}

export class ScenarioAlternateSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
}
