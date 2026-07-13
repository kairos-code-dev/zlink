import {
  zlinkStreamConnectorFactory,
  zlinkStreamJsonCodec,
  ZlinkStreamDispatchMode
} from '@zlink-systems/stream-connector';
import { runYdA1 } from './Scenarios/atd-a1-basic-terminator-scenario';
import { runYdA2 } from './Scenarios/atd-a2-await-terminator-scenario';
import { runYdA3 } from './Scenarios/atd-a3-continuation-context-scenario';
import { runYdA4 } from './Scenarios/atd-a4-worker-await-scenario';
import { bindAwaitActors, runYdB1 } from './Scenarios/atd-b1-other-actor-progress-scenario';
import { runYdB2 } from './Scenarios/atd-b2-same-actor-reentry-scenario';
import { runYdB3 } from './Scenarios/atd-b3-actor-join-await-scenario';
import { runYdC1 } from './Scenarios/atd-c1-timer-isolation-scenario';
import { runYdC2 } from './Scenarios/atd-c2-timer-reentry-scenario';
import { runYdC3 } from './Scenarios/atd-c3-actor-timer-isolation-scenario';
import { runYdD2 } from './Scenarios/atd-d2-remote-spot-await-scenario';
import { runYdD3 } from './Scenarios/atd-d3-route-bridge-await-scenario';
import { runYdD4 } from './Scenarios/atd-d4-session-relay-actor-await-scenario';
import { runYdE1 } from './Scenarios/atd-e1-timeout-scenario';
import { runYdE2 } from './Scenarios/atd-e2-cancellation-scenario';
import { runShutdownRecovery, runShutdownWait } from './Scenarios/shutdown-await-scenario';
import { parseClientOptions } from './Support/client-options';
import { browserE2eArgs, runBrowserE2e } from '../../browser-client-runtime';
import {
  AutomaticTurnDispatchNames,
  type AutomaticTurnDispatchRes,
  type AwaitReq,
  type EnsureSpotReq,
  type EnsureSpotRes
} from '../Shared/messages';

async function main(): Promise<void> {
  const options = parseClientOptions(browserE2eArgs());
  const scenario = options.scenario.toUpperCase();
  const full = scenario === 'FULL' || scenario === 'FULL-CORE';
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
    if (scenario !== 'ATD-D2' && scenario !== 'ATD-D4') {
      await waitForSpotRoutes(client, scenario);
    }
    if (full || scenario === 'ATD-A1') {
      const { spotRid } = await runYdA1(client);
      if (full) {
        await runYdA2(client, spotRid);
        await runYdA3(client, spotRid);
        await runYdA4(client, spotRid);
        const actors = await bindAwaitActors(client, spotRid);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await bindAwaitActors(peer, spotRid, actors, [actors.actorB]);
          await runYdB1(client, peer, actors);
        });
        await runYdB2(client, actors);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await bindAwaitActors(peer, spotRid, actors, [actors.actorB]);
          await runYdB3(client, peer, actors);
        });
        const timer = await runYdC1(client);
        await runYdC2(client, timer.spotRid);
        await bindAwaitActors(client, spotRid, actors, [actors.actorB]);
        await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
          await runYdC3(client, peer, actors);
        });
        await runYdD2(client);
        await runYdD4(client, () => createClient(options.sessionBStreamEndpoint), actors);
        console.log('scenario ATD-D1 passed');
        await runYdE1(client);
        await runYdE2(client);
        console.log('scenario ATD-E5 passed');
      }
    } else if (scenario === 'ATD-A2') {
      const { spotRid } = await runYdA1(client);
      await runYdA2(client, spotRid);
    } else if (scenario === 'ATD-A3') {
      const { spotRid } = await runYdA1(client);
      await runYdA3(client, spotRid);
    } else if (scenario === 'ATD-A4') {
      const { spotRid } = await runYdA1(client);
      await runYdA4(client, spotRid);
    } else if (scenario === 'ATD-B1') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await bindAwaitActors(peer, spotRid, actors, [actors.actorB]);
        await runYdB1(client, peer, actors);
      });
    } else if (scenario === 'ATD-B2') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await runYdB2(client, actors);
    } else if (scenario === 'ATD-B3') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await bindAwaitActors(peer, spotRid, actors, [actors.actorB]);
        await runYdB3(client, peer, actors);
      });
    } else if (scenario === 'ATD-C1') {
      await runYdC1(client);
    } else if (scenario === 'ATD-C2') {
      const timer = await runYdC1(client);
      await runYdC2(client, timer.spotRid);
    } else if (scenario === 'ATD-C3') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await runYdC3(client, peer, actors);
      });
    } else if (scenario === 'ATD-D2') {
      await runYdD2(client);
    } else if (scenario === 'ATD-D3') {
      await runYdD3(client);
    } else if (scenario === 'ATD-D4') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await runYdD4(client, () => createClient(options.sessionBStreamEndpoint), actors);
    } else if (scenario === 'ATD-D1') {
      const { spotRid } = await runYdA1(client);
      const actors = await bindAwaitActors(client, spotRid);
      await runYdC1(client);
      await withPeerClient(options.sessionAStreamEndpoint, async (peer) => {
        await runYdC3(client, peer, actors);
      });
      await runYdE1(client);
      console.log('scenario ATD-D1 passed');
    } else if (scenario === 'ATD-E1') {
      await runYdE1(client);
    } else if (scenario === 'ATD-E2') {
      await runYdE2(client);
    } else if (scenario === 'ATD-E5') {
      console.log('scenario ATD-E5 passed');
    } else {
      throw new Error(`Unknown scenario '${options.scenario}'.`);
    }
  } finally {
    await client.close();
  }

  console.log('await-dispatch client result=passed');
}

async function waitForSpotRoutes(client: ReturnType<typeof createClient>, scenario: string): Promise<void> {
  const targetNodeRids = [scenario === 'ATD-D3' ? 'play-b' : 'play-a'];
  for (const targetNodeRid of targetNodeRids) {
    const spotRid = `topology-${targetNodeRid}-${Date.now().toString(16)}-${Math.random().toString(16).slice(2)}`;
    const spot = await client
      .request({ spotRid } satisfies EnsureSpotReq)
      .packetName('EnsureSpotReq')
      .metadata(AutomaticTurnDispatchNames.targetNodeRidMetadata, targetNodeRid)
      .timeout(30000)
      .submit<EnsureSpotRes>();
    if (spot.nodeRid !== targetNodeRid) {
      throw new Error(`Topology probe expected ${targetNodeRid}, received ${spot.nodeRid}.`);
    }

    const deadline = Date.now() + 30000;
    while (true) {
      try {
        await client
          .request({
            requestId: `topology-${targetNodeRid}`,
            delayMs: 0,
            correlationId: 'topology-ready'
          } satisfies AwaitReq)
          .packetName('AwaitReq')
          .metadata(AutomaticTurnDispatchNames.spotRidMetadata, spotRid)
          .timeout(5000)
          .submit<AutomaticTurnDispatchRes>();
        break;
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (!/Host unreachable|timed out|not connected|disconnected/i.test(message) || Date.now() >= deadline) {
          throw new Error(`SPOT route to ${targetNodeRid} did not become ready: ${message}`);
        }
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
    }
  }
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

void runBrowserE2e('AutomaticTurnDispatch', main);
