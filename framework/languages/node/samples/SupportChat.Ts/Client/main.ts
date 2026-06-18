import * as connector from '@zlink-systems/stream-connector';
import { SupportChatClientScenario } from './supportchat-client-scenario';
import { SampleTimings } from './Configuration/sample-names';
import { loadSampleConfig } from './Configuration/sample-config';
import type { ZlinkStreamConnector } from '@zlink-systems/stream-connector';
async function main(): Promise<void> {
  const config = loadSampleConfig();

  const clients = {
    customer: createClient(config.sessionEndpoint, 'customer'),
    agent: createClient(config.sessionEndpoint, 'agent'),
    reconnectingCustomer: createClient(config.sessionEndpoint, 'reconnect'),
    waitingCustomer: createClient(config.sessionEndpoint, 'waiting'),
    closingCustomer: createClient(config.sessionEndpoint, 'close-customer'),
    closingAgent: createClient(config.sessionEndpoint, 'close-agent')
  };
  try {
    await new SupportChatClientScenario().run(clients);
  } finally {
    await Promise.allSettled(Object.values(clients).map((client) => client.close()));
  }

  console.log('PASS SupportChat.Ts');
}

function createClient(sessionEndpoint: string, clientName: string): ZlinkStreamConnector {
  const client = connector.zlinkStreamConnectorFactory.create({
    endpoint: sessionEndpoint,
    dispatchMode: connector.ZlinkStreamDispatchMode.Immediate,
    requestTimeoutMs: SampleTimings.requestTimeout,
    heartbeat: { enabled: false }
  });
  client.observeInbound((observation) => {
    console.log(
      `stream-inbound sample=SupportChat client=${clientName} kind=${observation.kind} ` +
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

export {};
