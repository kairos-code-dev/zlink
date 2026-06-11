import { TicTacToeClientScenario } from './tictactoe-client-scenario';
const { loadSampleConfig } = require('./Configuration/sample-config');

async function main(): Promise<void> {
  const config = loadSampleConfig();

  await new TicTacToeClientScenario().run(config.apiHttpEndpoint);

  console.log('PASS TicTacToe.Ts');
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
