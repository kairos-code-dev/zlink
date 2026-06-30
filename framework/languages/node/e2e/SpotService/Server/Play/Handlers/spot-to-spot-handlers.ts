import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  SlowSpotReply,
  SpotToSpotNegativeReply,
  SpotToSpotNegativeReq,
  SpotToSpotReply,
  SpotToSpotReq,
  SpotToSpotTimeoutReply,
  SpotToSpotTimeoutReq,
  StateReply
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
export class SpotToSpotHandler implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotReply> {
    void context;
    const reply = await spot.context.outbound
      .requestToSpot(request.targetSpotRid, { operation: 'add', delta: 3 })
      .packetName('StateReq')
      .submit<StateReply>();
    await spot.context.outbound
      .sendToSpot(request.targetSpotRid, { marker: `sm-c3-send-${request.marker}` })
      .packetName('StateCommand')
      .submit();
    await spot.context.outbound
      .publish(SpotServiceNames.spotEventTopic, { marker: `sm-c3-publish-${request.marker}` })
      .packetName('SpotEvent')
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
export class SpotToSpotTimeoutHandler
  implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotTimeoutReq, SpotToSpotTimeoutReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotTimeoutReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotTimeoutReply> {
    void context;
    let failed = false;
    try {
      await spot.context.outbound
        .requestToSpot(request.targetSpotRid, { marker: request.marker, delayMs: 1500 })
        .packetName('SlowSpotReq')
        .timeout(100)
        .submit<SlowSpotReply>();
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
export class SpotToSpotNegativeHandler
  implements ZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotNegativeReq, SpotToSpotNegativeReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotToSpotNegativeReq,
    context: ZLinkHandlerContext
  ): Promise<SpotToSpotNegativeReply> {
    void context;
    let requestFailed = false;
    try {
      await spot.context.outbound
        .requestToSpot(request.targetSpotRid, { operation: 'noop', delta: 0 })
        .packetName('MissingSpotReq')
        .timeout(2000)
        .submit<StateReply>();
    } catch {
      requestFailed = true;
    }
    await spot.context.outbound
      .sendToSpot(request.targetSpotRid, { marker: `missing-${request.marker}` })
      .packetName('MissingSpotCommand')
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
