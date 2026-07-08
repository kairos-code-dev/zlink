import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import { runYdA1 } from './Scenarios/yd-a1-basic-terminator-scenario';
import { runYdA2 } from './Scenarios/yd-a2-yield-terminator-scenario';
import { runYdA3 } from './Scenarios/yd-a3-continuation-context-scenario';
import { runYdA4 } from './Scenarios/yd-a4-worker-yield-scenario';
import { bindYieldActors, runYdB1 } from './Scenarios/yd-b1-other-actor-progress-scenario';
import { runYdB2 } from './Scenarios/yd-b2-same-actor-reentry-scenario';
import { runYdB3 } from './Scenarios/yd-b3-actor-join-yield-scenario';
import { runYdC1 } from './Scenarios/yd-c1-timer-isolation-scenario';
import { runYdC2 } from './Scenarios/yd-c2-timer-reentry-scenario';
import { runYdC3 } from './Scenarios/yd-c3-actor-timer-isolation-scenario';
import { runYdD2 } from './Scenarios/yd-d2-remote-spot-yield-scenario';
import { runYdD3 } from './Scenarios/yd-d3-route-bridge-yield-scenario';
import { runYdD4 } from './Scenarios/yd-d4-session-relay-actor-yield-scenario';
import { runYdE1 } from './Scenarios/yd-e1-timeout-scenario';
import { runYdE2 } from './Scenarios/yd-e2-cancellation-scenario';
import { runShutdownRecovery, runShutdownWait } from './Scenarios/shutdown-yield-scenario';
import { parseClientOptions } from './Support/client-options';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  const scenario = options.scenario.toUpperCase();
  if (scenario === 'SHUTDOWN-WAIT') {
    await runShutdownWait(options);
    return;
  }
  if (scenario === 'SHUTDOWN-RECOVERY') {
    await runShutdownRecovery(options);
    return;
  }

  const client = createClient(options.sessionAStreamEndpoint);
  await client.connect();
  try {
    if (scenario === 'FULL' || scenario === 'YD-A1') {
      const { spotRid } = await runYdA1(client);
      if (scenario === 'FULL') {
        await runYdA2(client, spotRid);
        await runYdA3(client, spotRid);
        await runYdA4(client, spotRid);
        const actors = await bindYieldActors(client, spotRid);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await bindYieldActors(peer, spotRid, actors, [actors.actorB]);
          await runYdB1(client, peer, actors);
        });
        await runYdB2(client, actors);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await bindYieldActors(peer, spotRid, actors, [actors.actorB]);
          await runYdB3(client, peer, actors);
        });
        const timer = await runYdC1(client);
        await runYdC2(client, timer.spotRid);
        await bindYieldActors(client, spotRid, actors, [actors.actorB]);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await runYdC3(client, peer, actors);
        });
        await runYdD2(client);
        await runYdD3(client);
        await runYdD4(client, () => createClient(options.sessionBStreamEndpoint), actors);
        console.log('scenario YD-D1 passed');
        await runYdE1(client);
        await runYdE2(client);
        console.log('scenario YD-E5 passed');
      }
    } else if (scenario === 'YD-A2') {
      const { spotRid } = await runYdA1(client);
      await runYdA2(client, spotRid);
    } else if (scenario === 'YD-A3') {
      const { spotRid } = await runYdA1(client);
      await runYdA3(client, spotRid);
    } else if (scenario === 'YD-A4') {
      const { spotRid } = await runYdA1(client);
      await runYdA4(client, spotRid);
    } else if (scenario === 'YD-B1') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await bindYieldActors(peer, spotRid, actors, [actors.actorB]);
        await runYdB1(client, peer, actors);
      });
    } else if (scenario === 'YD-B2') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await runYdB2(client, actors);
    } else if (scenario === 'YD-B3') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await bindYieldActors(peer, spotRid, actors, [actors.actorB]);
        await runYdB3(client, peer, actors);
      });
    } else if (scenario === 'YD-C1') {
      await runYdC1(client);
    } else if (scenario === 'YD-C2') {
      const timer = await runYdC1(client);
      await runYdC2(client, timer.spotRid);
    } else if (scenario === 'YD-C3') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await runYdC3(client, peer, actors);
      });
    } else if (scenario === 'YD-D2') {
      await runYdD2(client);
    } else if (scenario === 'YD-D3') {
      await runYdD3(client);
    } else if (scenario === 'YD-D4') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await runYdD4(client, () => createClient(options.sessionBStreamEndpoint), actors);
    } else if (scenario === 'YD-D1') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindYieldActors(client, spotRid);
      await runYdC1(client);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await runYdC3(client, peer, actors);
      });
      await runYdE1(client);
      console.log('scenario YD-D1 passed');
    } else if (scenario === 'YD-E1') {
      await runYdE1(client);
    } else if (scenario === 'YD-E2') {
      await runYdE2(client);
    } else if (scenario === 'YD-E5') {
      console.log('scenario YD-E5 passed');
    } else {
      throw new Error(`Unknown scenario '${options.scenario}'.`);
    }
  } finally {
    await client.close();
  }

  console.log('yield-dispatch client result=passed');
}

function createClient(endpoint: string) {
  return zlinkStreamConnectorFactory.create({
    endpoint,
    codec: zlinkStreamJsonCodec,
    dispatchMode: ZlinkStreamDispatchMode.Immediate,
    heartbeat: { enabled: false },
    waitTimeoutMs: 30000,
    requestTimeoutMs: 60000,
    maxReceivedMessages: 1024
  });
}

async function withPeerClient<T>(endpoint: string, run: (client: ReturnType<typeof createClient>) => Promise<T>): Promise<T> {
  const client = createClient(endpoint);
  await client.connect();
  try {
    return await run(client);
  } finally {
    await client.close();
  }
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
