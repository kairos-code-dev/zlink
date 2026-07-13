import * as connector from '@zlink-systems/stream-connector';
import { bingoProtobuf } from '../Shared/Contracts/protobuf-browser-codec';
import { BingoClientScenario } from './bingo-client-scenario';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import { SampleTimings } from './Configuration/sample-names';
import { loadSampleConfig } from './Configuration/sample-config';
import { runBrowserSample } from '../../browser-client-runtime';
async function main(): Promise<void> {
  const config = await loadSampleConfig();

  const client1 = createClient(config.sessionAEndpoint, 'player-1');
  const client2 = createClient(config.sessionBEndpoint, 'player-2');
  const observer = createClient(config.sessionBEndpoint, 'observer');
  try {
    await new BingoClientScenario().run(client1, client2, observer);
  } finally {
    await Promise.allSettled([
      client1.close(),
      client2.close(),
      observer.close()
    ]);
  }

  console.log('bingo=completed');
}

function createClient(sessionEndpoint: string, clientName: string): ZlinkStreamConnector {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    codec: bingoProtobuf,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    waitTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  client.observeInbound((observation) => {
    console.log(
      `stream-inbound sample=Bingo client=${clientName} kind=${observation.kind} ` +
      `name=${observation.name} seq=${observation.requestSeq?.toString() ?? '-'} ` +
      `flow=${observation.flowId ?? '-'} bytes=${observation.payloadLength}`
    );
  });
  return client;
}

void runBrowserSample('Bingo.Ts', main);
