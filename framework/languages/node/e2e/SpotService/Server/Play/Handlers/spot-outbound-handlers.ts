import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotSubscriptionHandler } from '@zlink-systems/framework';
import type {
  ChannelEchoRes,
  SpotMsg,
  SpotOutboundNegativeMsg,
  SpotOutboundMsg
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { ScenarioUserSpot } from '../Spots/scenario-spots';

@Injectable()
export class SpotOutboundHandler implements ZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotOutboundMsg,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    const echo = await spot.context.outbound
      .requestToChannel(SpotServiceNames.externalClientChannel, { value: request.marker })
      .packetName('ChannelEchoReq')
      .submit<ChannelEchoRes>();
    const notifyMarker = `notify-${request.marker}`;
    await spot.context.outbound
      .sendToChannel(SpotServiceNames.externalClientChannel, { marker: notifyMarker })
      .packetName('ChannelNotify')
      .submit();
    await spot.context.outbound
      .publish(SpotServiceNames.spotEventTopic, { marker: 'sm-c2-publish' })
      .packetName('SpotMsg')
      .submit();
    this.evidence.add(
      `spot-outbound|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
      + `|echo=${echo.value}|notify=${notifyMarker}`
    );
  }
}

@Injectable()
export class SpotOutboundNegativeHandler implements ZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundNegativeMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: ScenarioUserSpot,
    request: SpotOutboundNegativeMsg,
    context: ZLinkHandlerContext
  ): Promise<void> {
    void context;
    let requestFailed = false;
    try {
      await spot.context.outbound
        .requestToChannel(SpotServiceNames.externalClientChannel, { value: request.marker })
        .packetName('MissingChannelReq')
        .timeout(2000)
        .submit<ChannelEchoRes>();
    } catch {
      requestFailed = true;
    }
    await spot.context.outbound
      .sendToChannel(SpotServiceNames.externalClientChannel, { marker: `missing-${request.marker}` })
      .packetName('MissingChannelNotify')
      .submit();
    this.evidence.add(
      `spot-outbound-negative|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|requestFailed=${requestFailed ? 'True' : 'False'}`
    );
  }
}

@Injectable()
export class SpotMsgHandler implements ZLinkSpotSubscriptionHandler<ScenarioUserSpot, SpotMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: ScenarioUserSpot, event: SpotMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(`spot-msg|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|marker=${event.marker}`);
  }
}
