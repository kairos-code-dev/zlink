import * as fs from 'node:fs';
type BingoSampleConfig = {
  sessionAEndpoint: string;
  sessionBEndpoint: string;
};

function loadSampleConfig(): BingoSampleConfig {
  const configPath = process.env.ZLINK_SAMPLE_CONFIG;
  if (configPath !== undefined && configPath.length > 0) {
    const sample = JSON.parse(fs.readFileSync(configPath, 'utf8')).sample;
    return {
      sessionAEndpoint: sample.sessionAEndpoint ?? sample.sessionEndpoint,
      sessionBEndpoint: sample.sessionBEndpoint ?? sample.sessionEndpoint
    };
  }
  return {
    sessionAEndpoint: process.env.BINGO_SESSION_A_ENDPOINT ?? requireEnv('BINGO_SESSION_ENDPOINT'),
    sessionBEndpoint: process.env.BINGO_SESSION_B_ENDPOINT ?? process.env.BINGO_SESSION_ENDPOINT ?? requireEnv('BINGO_SESSION_B_ENDPOINT')
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
