import { Inject, Injectable, type OnApplicationBootstrap, type OnApplicationShutdown } from '@nestjs/common';
import { ZLinkSpotEventKind } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLINK_ROUTE_MESH_RUNTIME, zlinkRuntimeEventHandler } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient, ZLinkRouteMeshRuntime, ZLinkRuntimeEventHandler, ZLinkSpotEvent } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';
import { ReportSpotEventMsg } from '../../../../../Shared/contracts';
import { NodeAlertKinds, ZoneWorldNames, zonesOf } from '../../../../../Shared/spec';

@Injectable()
@zlinkRuntimeEventHandler()
class SpotRuntimeEventHandler implements ZLinkRuntimeEventHandler<ZLinkSpotEvent>, OnApplicationBootstrap, OnApplicationShutdown {
  private readonly stop = new AbortController();

  constructor(
    @Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    @Inject(ZLINK_ROUTE_MESH_RUNTIME) private readonly routeMeshRuntime: ZLinkRouteMeshRuntime
  ) {}

  onApplicationBootstrap(): void {
    void this.observeReadiness();
  }

  onApplicationShutdown(): void {
    this.stop.abort();
  }

  async handle(event: ZLinkSpotEvent): Promise<void> {
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined) return;
    if (event.event !== ZLinkSpotEventKind.TimerHandlerFailed) return;
    if (!zonesOf(nodeId).some((zoneId) => zoneId === String(event.timerDiagnostic.spotId))) return;
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

  private async observeReadiness(): Promise<void> {
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined) return;
    for await (const event of this.routeMeshRuntime.observe(ZoneWorldNames.zoneMesh, 64, this.stop.signal)) {
      if (event.identifier !== 'zlink.runtime.mesh_node.peer_changed') continue;
      console.log(`mesh peer changed node=${nodeId} peer=${event.peerRid ?? '-'} reason=${event.reason ?? '-'}`);
    }
  }

}

export { SpotRuntimeEventHandler };
