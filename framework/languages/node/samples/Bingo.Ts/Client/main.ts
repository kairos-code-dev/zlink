import * as connector from '@zlink-systems/stream-connector';
import { bingoProtobufCodec } from '../Shared/Contracts/protobuf-codec';
import { BingoClientScenario } from './bingo-client-scenario';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
import { SampleTimings } from './Configuration/sample-names';
import { loadSampleConfig } from './Configuration/sample-config';
async function main(): Promise<void> {
  const config = loadSampleConfig();

  const client1 = createClient(config.sessionEndpoint, 'player1');
  const client2 = createClient(config.sessionEndpoint, 'player2');
  try {
    await new BingoClientScenario().run(client1, client2);
  } finally {
    await Promise.allSettled([
      client1.close(),
      client2.close()
    ]);
  }

  console.log('PASS Bingo.Ts');
}

function createClient(sessionEndpoint: string, clientName: string): ZlinkStreamConnector {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    codec: bingoProtobufCodec,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  client.observeInbound((observation) => {
    console.log(
      `stream-inbound sample=Bingo client=${clientName} kind=${observation.kind} ` +
      `name=${observation.name} seq=${observation.requestSeq?.toString() ?? '-'} ` +
      `bytes=${observation.payloadLength}`
    );
  });
  return client;
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
