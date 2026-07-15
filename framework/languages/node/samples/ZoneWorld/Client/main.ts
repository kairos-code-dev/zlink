import * as fs from 'node:fs';
import {
  ZlinkStreamDispatchMode,
  zlinkStreamAssert,
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec
} from '@zlink-systems/stream-connector';
import { JoinWorldReq, MoveMsg, PacketNames } from '../Shared/contracts';
import { NodeIds, ZoneIds, ZoneWorldSpec } from '../Shared/spec';
import { readConfigPath, validateConfiguration } from '../Server/Configuration/configuration';
import type { JoinWorldRes, MoveRejectedNotify, ZoneStateNotify } from '../Shared/contracts';

async function main(): Promise<void> {
  const path = readConfigPath(process.argv.slice(2));
  const config = validateConfiguration(JSON.parse(fs.readFileSync(path, 'utf8')) as unknown, 'client');
  if (config.client === undefined) throw new Error('Client configuration is required.');
  const gateway = zlinkStreamConnectorFactory.create({
    endpoint: config.client.gatewayEndpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    waitTimeoutMs: 10_000,
    heartbeat: { enabled: false }
  });
  try {
    await gateway.connect();
    const joined = await gateway
      .request(new JoinWorldReq('player-a1'))
      .packetName(PacketNames.joinWorldReq)
      .submit<JoinWorldRes>();
    zlinkStreamAssert.ensure(joined.playerId === 'player-a1', 'ZW-A1 player id mismatch.');
    zlinkStreamAssert.ensure(joined.zoneId === ZoneIds.northWest, 'ZW-A1 spawn zone mismatch.');
    zlinkStreamAssert.ensure(joined.nodeId === NodeIds.west, 'ZW-A1 spawn node mismatch.');
    zlinkStreamAssert.ensure(
      joined.x === ZoneWorldSpec.spawnX && joined.y === ZoneWorldSpec.spawnY,
      'ZW-A1 spawn coordinate mismatch.'
    );
    zlinkStreamAssert.ensure(joined.error === null, 'ZW-A1 join was rejected.');
    console.log('scenario ZW-A1 passed');

    const rejectedTask = gateway
      .waitFor<MoveRejectedNotify>(PacketNames.moveRejectedNotify)
      .where((message) => message.payload.reason === 'OutOfRange')
      .submit();
    await gateway.send(new MoveMsg(-40, joined.y)).packetName(PacketNames.moveMsg).submit();
    const rejected = await rejectedTask;
    zlinkStreamAssert.ensure(
      rejected.payload.x === joined.x && rejected.payload.y === joined.y,
      'ZW-A2 rejection changed the player coordinate.'
    );
    console.log('scenario ZW-A2 passed');

    const movedTask = gateway
      .waitFor<ZoneStateNotify>(PacketNames.zoneStateNotify)
      .where((message) => message.payload.players.some((player) =>
        player.playerId === joined.playerId && player.x === joined.x + 4 && player.y === joined.y))
      .submit();
    await gateway.send(new MoveMsg(joined.x + 4, joined.y)).packetName(PacketNames.moveMsg).submit();
    await movedTask;
    console.log('scenario ZW-A5 passed');
  } finally {
    await gateway.close();
  }
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
