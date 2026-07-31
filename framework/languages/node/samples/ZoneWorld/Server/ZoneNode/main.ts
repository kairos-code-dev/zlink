import 'reflect-metadata';
import * as fs from 'node:fs';
import { setTimeout as delay } from 'node:timers/promises';
import { NestFactory } from '@nestjs/core';
import {
  ZLINK_ACTOR_CLIENT,
  ZLINK_ACTOR_MANAGER,
  ZLINK_ROUTE_CLIENT,
  ZLINK_SPOT_MANAGER
} from '@zlink-systems/nestjs';
import type {
  ZLinkActorClient,
  ZLinkActorManager,
  ZLinkRouteClient,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import { ZONEWORLD_CONFIG } from '../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../Configuration/configuration';
import { closeRuntime, waitForShutdown } from '../runtime-support';
import { ZoneWorldNames, zonesOf } from '../../Shared/spec';
import { createZoneNodeModule } from './zone-node-module';
import { ZoneSpot } from './Infrastructure/ZLink/Spots/zone-spot';
import { EnterWorldReq, ReportNodeStatusMsg } from '../../Shared/contracts';
import { MaintenanceStore } from '../Configuration/maintenance-store';
import { NodeRuntimeState } from './Domain/node-runtime-state';
import { botRoutes } from './Domain/bot-patrol';

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
    const state = app.get(NodeRuntimeState);
    const maintenance = app.get(MaintenanceStore);
    state.restore(await maintenance.readAll());
    console.log(`maintenance restored node=${node.nodeId} enabled=${state.ownMaintenance()}`);
    const spots = app.get<ZLinkSpotManager>(ZLINK_SPOT_MANAGER, { strict: false });
    for (const zoneId of zones) {
      await spots
        .getOrCreate(zoneId, ZoneSpot.name)
        .inMesh(ZoneWorldNames.zoneMesh)
        .submit();
    }
    if (node.disableBots !== true) {
      await spawnBots(app, zones);
      console.log(`bot-start=ready node=${node.nodeId}`);
      await waitForBotStart(node.botStartSignalPath);
    }
    state.enableBotTicks();
    const channels = app.get<ZLinkRouteClient>(ZLINK_ROUTE_CLIENT, { strict: false });
    const report = async () => {
      try {
        await channels.sendToChannel(
          ZoneWorldNames.reportChannel,
          new ReportNodeStatusMsg(
            node.nodeId,
            [...zones],
            state.playerCount(),
            state.ownMaintenance()
          )
        ).submit();
        console.log(`node status submitted node=${node.nodeId}`);
      } catch {
        // Ops may start after this node; the periodic report retries through the public channel.
      }
    };
    statusTimer = setInterval(() => { void report(); }, 1_000);
    await report();
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

async function waitForBotStart(signalPath: string | undefined): Promise<void> {
  if (signalPath === undefined) return;
  while (!fs.existsSync(signalPath)) await delay(50);
}

async function spawnBots(app: { get<T>(token: unknown, options?: { strict: boolean }): T }, zones: readonly string[]): Promise<void> {
  const manager = app.get<ZLinkActorManager>(ZLINK_ACTOR_MANAGER, { strict: false });
  const client = app.get<ZLinkActorClient>(ZLINK_ACTOR_CLIENT, { strict: false });
  for (const route of botRoutes.filter((candidate) => zones.includes(candidate.zoneId))) {
    if (await manager.find(route.playerId) !== undefined) continue;
    const result = await manager
      .getOrCreate(route.playerId, ZoneWorldNames.playerActorType)
      .inMesh(ZoneWorldNames.zoneMesh)
      .submit();
    if (result.status === 'rejected') {
      throw new Error(`Bot actor '${route.playerId}' creation was rejected.`);
    }
    const actor = result.actor;
    const entered = await client.requestToActor(
      actor.actorId,
      new EnterWorldReq(route.x, route.y, true, route.dirX, route.dirY)
    ).timeout(10_000).submit<{ error: string | null }>();
    if (entered.error !== null) throw new Error(`Bot '${route.playerId}' could not enter the world: ${entered.error}.`);
    console.log(`bot spawned bot=${route.playerId} zone=${route.zoneId}`);
  }
}
