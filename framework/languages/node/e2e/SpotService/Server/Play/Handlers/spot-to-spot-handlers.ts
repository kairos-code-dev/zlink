import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { ZLinkPacket } from '@zlink-systems/framework';
import type {
  SlowSpotRes,
  SpotToSpotNegativeRes,
  SpotToSpotNegativeReq,
  SpotToSpotRes,
  SpotToSpotReq,
  SpotToSpotTimeoutRes,
  SpotToSpotTimeoutReq,
  StateRes
} from '../../../Shared/messages';
import {
  MissingSpotMsg,
  MissingSpotReq,
  SlowSpotReq,
  SpotMsg,
  SpotServiceNames,
  StateMsg,
  StateReq,
  spotServicePacket
} from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
@ZLinkPacket('SpotToSpotReq')
export class SpotToSpotHandler implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotRes> {
    void context;
    const reply = await spot.context.outbound
      .requestToSpot(request.targetSpot, spotServicePacket(StateReq, { operation: 'add', delta: 3 }))
      .submit<StateRes>();
    await spot.context.outbound
      .sendToSpot(request.targetSpot,
        spotServicePacket(StateMsg, { marker: `sm-c3-send-${request.marker}` }))
      .submit();
    await spot.context.outbound
      .publish(SpotServiceNames.spotChannel, SpotServiceNames.spotEventTopic,
        spotServicePacket(SpotMsg, { marker: `sm-c3-publish-${request.marker}` }))
      .submit();
    this.evidence.add(
      `spot-to-spot|rid=${this.evidence.rid}|source=${spot.context.spotRid}`
      + `|target=${request.targetSpotRid}|value=${reply.value}`
    );
    return {
      sourceSpotRid: String(spot.context.spotRid),
      targetSpotRid: request.targetSpotRid,
      targetValue: reply.value
    };
  }
}

@Injectable()
@ZLinkPacket('SpotToSpotTimeoutReq')
export class SpotToSpotTimeoutHandler
  implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotTimeoutReq, SpotToSpotTimeoutRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotTimeoutReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotTimeoutRes> {
    void context;
    let failed = false;
    try {
      await spot.context.outbound
        .requestToSpot(request.targetSpot,
          spotServicePacket(SlowSpotReq, { marker: request.marker, delayMs: 1500 }))
        .timeout(100)
        .submit<SlowSpotRes>();
    } catch {
      failed = true;
    }
    this.evidence.add(
      `spot-to-spot-timeout|rid=${this.evidence.rid}|source=${spot.context.spotRid}`
      + `|target=${request.targetSpotRid}|failed=${failed ? 'True' : 'False'}`
    );
    return {
      sourceSpotRid: String(spot.context.spotRid),
      targetSpotRid: request.targetSpotRid,
      failed
    };
  }
}

@Injectable()
@ZLinkPacket('SpotToSpotNegativeReq')
export class SpotToSpotNegativeHandler
  implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotNegativeReq, SpotToSpotNegativeRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotNegativeReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotNegativeRes> {
    void context;
    let requestFailed = false;
    try {
      await spot.context.outbound
        .requestToSpot(request.targetSpot,
          spotServicePacket(MissingSpotReq, { operation: 'noop', delta: 0 }))
        .timeout(2000)
        .submit<StateRes>();
    } catch {
      requestFailed = true;
    }
    await spot.context.outbound
      .sendToSpot(request.targetSpot,
        spotServicePacket(MissingSpotMsg, { marker: `missing-${request.marker}` }))
      .submit();
    this.evidence.add(
      `spot-to-spot-negative|rid=${this.evidence.rid}|source=${spot.context.spotRid}`
      + `|target=${request.targetSpotRid}|requestFailed=${requestFailed ? 'True' : 'False'}`
    );
    return {
      sourceSpotRid: String(spot.context.spotRid),
      targetSpotRid: request.targetSpotRid,
      requestFailed
    };
  }
}
