import { Injectable } from '@nestjs/common';
import type {
  ZLinkHandlerContext,
  ZLinkSpotPacketHandler,
  ZLinkSpotRequestHandler,
  ZLinkSpotTimerHandler,
  ZLinkTimerTick
} from '@zlink-systems/framework';
import type { StageProbeReq, StageTimerStartCommand, StateReply } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { ScenarioUserSpot } from '../Spots/scenario-spots';

class ScenarioStage {
  constructor(private readonly spot: ScenarioUserSpot) {}

  apply(request: StageProbeReq, evidence: EvidenceStore): StateReply {
    const value = this.spot.add(request.delta);
    evidence.add(
      `stage-request|rid=${evidence.rid}|spot=${this.spot.context.spotRid}`
      + `|marker=${request.marker}|value=${value}`
    );
    return {
      spotRid: String(this.spot.context.spotRid),
      nodeRid: String(this.spot.context.nodeRid),
      value
    };
  }

  async startTimer(command: StageTimerStartCommand): Promise<void> {
    await this.spot.context.addTimer(command.name, command.periodMs, StageTimerHandler);
  }
}

@Injectable()
export class StageProbeHandler implements ZLinkSpotRequestHandler<ScenarioUserSpot, StageProbeReq, StateReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: StageProbeReq,
    context: ZLinkHandlerContext
  ): Promise<StateReply> {
    void context;
    return new ScenarioStage(spot).apply(request, this.evidence);
  }
}

@Injectable()
export class StageTimerStartHandler implements ZLinkSpotPacketHandler<ScenarioUserSpot, StageTimerStartCommand> {
  async handle(
    spot: ScenarioUserSpot,
    message: StageTimerStartCommand,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    await new ScenarioStage(spot).startTimer(message);
  }
}

@Injectable()
export class StageTimerHandler implements ZLinkSpotTimerHandler<ScenarioUserSpot> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: ScenarioUserSpot, tick: ZLinkTimerTick): Promise<void> {
    this.evidence.add(
      `stage-timer|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|name=${tick.name}`
      + `|delivery=${tick.deliveryIndex}`
    );
  }
}
