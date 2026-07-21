import { Inject, Injectable } from '@nestjs/common';
import { ZLinkSpotEventKind } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient, ZLinkRuntimeEventHandler, ZLinkSpotEvent } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';
import { ReportSpotEventMsg } from '../../../../../Shared/contracts';
import { NodeAlertKinds, ZoneWorldNames, zonesOf } from '../../../../../Shared/spec';

@Injectable()
@zlinkRuntimeEventHandler()
class SpotRuntimeEventHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent> {
  constructor(
    @Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  async handle(event: ZLinkSpotEvent): Promise<void> {
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined) return;
    if (event.event === ZLinkSpotEventKind.PeersChanged) {
      if (event.sourceName !== ZoneWorldNames.zoneMesh) return;
      if (event.peers.length > 0) console.log(`spot peers ready node=${nodeId} peers=${event.peers.length}`);
      return;
    }
    if (event.event !== ZLinkSpotEventKind.TimerHandlerFailed) return;
    if (!zonesOf(nodeId).some((zoneId) => zoneId === String(event.timerDiagnostic.spotRid))) return;
    await this.channels.sendToChannel(
      ZoneWorldNames.zoneMesh,
      ZoneWorldNames.reportChannel,
      new ReportSpotEventMsg(
        nodeId,
        NodeAlertKinds.timerHandlerFailed,
        `${event.timerDiagnostic.timerName}: ${event.timerDiagnostic.exceptionMessage}`,
        event.timestamp.toISOString()
      )
    ).submit();
    console.log(
      `spot event reported node=${nodeId} kind=${NodeAlertKinds.timerHandlerFailed}`
      + ` timer=${event.timerDiagnostic.timerName} error=${event.timerDiagnostic.exceptionMessage}`
    );
  }

}

export { SpotRuntimeEventHandler };
