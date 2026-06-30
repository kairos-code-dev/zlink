import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type { SlowSpotReply, SlowSpotReq, StateCommand, StateReply, StateReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
export class StateReqHandler implements ZLinkSpotRequestHandler<ScenarioUserSpot, StateReq, StateReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: StateReq,
    context: ZLinkHandlerContext
  ): Promise<StateReply> {
    void context;
    const delta = request.operation === 'add' ? request.delta : 0;
    const value = spot.add(delta);
    this.evidence.add(`spot-state-request|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|value=${value}`);
    return {
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      value
    };
  }
}

@Injectable()
export class StateCommandHandler implements ZLinkSpotPacketHandler<ScenarioUserSpot, StateCommand> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    message: StateCommand,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    this.evidence.add(
      `spot-state-command|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|marker=${message.marker}`
    );
  }
}

@Injectable()
export class SlowSpotHandler implements ZLinkSpotRequestHandler<ScenarioUserSpot, SlowSpotReq, SlowSpotReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SlowSpotReq,
    context: ZLinkHandlerContext
  ): Promise<SlowSpotReply> {
    void context;
    await new Promise((resolve) => setTimeout(resolve, request.delayMs));
    this.evidence.add(
      `slow-spot-request|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|marker=${request.marker}`
    );
    return {
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      marker: request.marker
    };
  }
}
