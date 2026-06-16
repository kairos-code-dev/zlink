import * as fs from 'node:fs';
type TicTacToeSampleConfig = {
  apiHttpEndpoint: string;
};

function loadSampleConfig(): TicTacToeSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    return JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
  }
  return {
    apiHttpEndpoint: requireEnv('TICTACTOE_API_HTTP_ENDPOINT')
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
