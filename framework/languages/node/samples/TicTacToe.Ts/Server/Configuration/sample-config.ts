const fs = require('node:fs');

type TicTacToeSampleConfig = {
  apiEndpoint: string;
  apiHttpEndpoint: string;
  playEndpoint: string;
  playStreamEndpoint: string;
};

function loadSampleConfig(): TicTacToeSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    apiEndpoint: requireEnv('TICTACTOE_API_ENDPOINT'),
    apiHttpEndpoint: requireEnv('TICTACTOE_API_HTTP_ENDPOINT'),
    playEndpoint: requireEnv('TICTACTOE_PLAY_ENDPOINT'),
    playStreamEndpoint: requireEnv('TICTACTOE_PLAY_STREAM_ENDPOINT')
  };
}

function requireEnv(name: string): string {
  const value = process.env[name];
  if (value === undefined || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

export { loadSampleConfig };
export type { TicTacToeSampleConfig };
