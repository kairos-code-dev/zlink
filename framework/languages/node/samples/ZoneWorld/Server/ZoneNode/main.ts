import 'reflect-metadata';
import { NestFactory } from '@nestjs/core';
import { ZLINK_CHANNEL_CLIENT, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient, ZLinkSpotManager } from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG } from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { reportRoutingAllocation } from '../Configuration/routing-id-report';
import { closeRuntime, waitForShutdown } from '../runtime-support';
import { ZoneWorldNames, zonesOf } from '../../Shared/spec';
import { createZoneNodeModule, ZONE_NODE_ALLOCATION_GROUP } from './zone-node-module';
import { ZoneSpot } from './Infrastructure/ZLink/Spots/zone-spot';
import { ReportNodeStatusMsg } from '../../Shared/contracts';

let statusTimer: NodeJS.Timeout | undefined;

async function bootstrap(): Promise<void> {
  const ZoneNodeModule = createZoneNodeModule();
  const app = await NestFactory.createApplicationContext(ZoneNodeModule, {
    logger: false,
    abortOnError: false
  });
  const config = app.get<ZoneWorldConfiguration>(ZONEWORLD_CONFIG);
  const node = config.zoneNode;
  if (node === undefined) throw new Error('ZoneNode configuration is required.');
  const zones = zonesOf(node.nodeId);
  if (zones.length > 0) {
    const allocation = await reportRoutingAllocation(app, node.nodeId, ZONE_NODE_ALLOCATION_GROUP, [
      ZoneWorldNames.zoneMesh,
      ZoneWorldNames.bridgeMesh,
      ZoneWorldNames.reportChannel
    ]);
    const spots = app.get<ZLinkSpotManager>(ZLINK_SPOT_MANAGER, { strict: false });
    for (const zoneId of zones) await spots.getOrCreate(ZoneSpot, zoneId);
    const channels = app.get<ZLinkChannelClient>(ZLINK_CHANNEL_CLIENT, { strict: false });
    const report = () => {
      try {
        channels.sendToChannel(
          ZoneWorldNames.reportChannel,
          new ReportNodeStatusMsg(
            node.nodeId,
            String(allocation.memberRoutingIds.get(ZoneWorldNames.reportChannel)),
            [...zones],
            0,
            false
          )
        ).submit();
        console.log(`node status submitted node=${node.nodeId}`);
      } catch {
        // Ops may start after this node; the periodic report retries through the public channel.
      }
    };
    statusTimer = setInterval(report, 1_000);
    report();
  }
  console.log(`topology=ready node=${node.nodeId} zones=${zones.join(',')}`);
  try {
    await waitForShutdown();
  } finally {
    if (statusTimer !== undefined) clearInterval(statusTimer);
    await closeRuntime(app);
  }
}

bootstrap().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
