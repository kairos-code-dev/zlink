import { parseClientOptions } from './Support/client-options';
import { ServerProcessLauncher, type DynamicProcess } from './Support/server-process-launcher';
import { runPsA1 } from './Scenarios/ps-a1-fanout-basic-delivery-scenario';
import { runPsA2 } from './Scenarios/ps-a2-topic-filter-scenario';
import { runPsA3 } from './Scenarios/ps-a3-late-subscriber-scenario';
import { runPsA4 } from './Scenarios/ps-a4-subscriber-reconnect-scenario';
import { runPsB1 } from './Scenarios/ps-b1-slow-subscriber-scenario';
import { runPsB2 } from './Scenarios/ps-b2-publisher-restart-scenario';
import { runPsC1 } from './Scenarios/ps-c1-missing-message-name-scenario';

async function main(): Promise<void> {
  const options = parseClientOptions(process.argv.slice(2));
  const processes = new ServerProcessLauncher(options);
  let restartedPublisher: DynamicProcess | undefined;
  const subscribers = options.subscriberUrls;

  const scenarios: Record<string, () => Promise<void>> = {
    'PS-A1': () => runPsA1(options.publisherUrl, subscribers),
    'PS-A2': () => runPsA2(options.publisherUrl, subscribers),
    'PS-A3': () => runPsA3(options.publisherUrl, options.lateSubscriberUrl, processes, options.publisherEndpoint),
    'PS-A4': () => runPsA4(options.publisherUrl, options.lateSubscriberUrl, subscribers.slice(0, 2), processes, options.publisherEndpoint),
    'PS-B1': () => runPsB1(options.publisherUrl, subscribers.slice(0, 2), subscribers[subscribers.length - 1]),
    'PS-B2': async () => { restartedPublisher = await runPsB2(options.publisherUrl, subscribers, processes, options.publisherEndpoint); },
    'PS-C1': () => runPsC1(options.publisherUrl, subscribers)
  };

  try {
    if (options.scenario.toLowerCase() === 'all') {
      for (const scenario of Object.values(scenarios)) {
        await scenario();
      }
    } else {
      const selected = scenarios[options.scenario.toUpperCase()];
      if (selected === undefined) {
        throw new Error(`Unknown scenario '${options.scenario}'.`);
      }
      await selected();
    }
  } finally {
    if (restartedPublisher !== undefined && !restartedPublisher.hasExited) {
      await restartedPublisher.stop();
    }
  }

  console.log('pubsub e2e result=passed');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
