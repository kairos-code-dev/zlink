import { loadSampleConfig } from './Configuration/sample-config';
import { SupportChatClientScenario } from './supportchat-client-scenario';

async function main(): Promise<void> {
  const config = loadSampleConfig();
  await new SupportChatClientScenario(config.apiHttpUrl).run();
  console.log('supportchat=completed');
  console.log('PASS SupportChat.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
