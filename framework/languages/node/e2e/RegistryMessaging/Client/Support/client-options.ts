export interface ClientOptions {
  readonly providerAUrl: string;
  readonly providerBUrl: string;
  readonly workflowUrl: string;
  readonly directConsumerUrl: string;
  readonly singleConsumerUrl: string;
  readonly backpressureConsumerUrl: string;
  readonly locationConsumerUrl: string;
  readonly providerMain: string;
  readonly consumerMain: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly logDir: string;
  readonly scenario: string;
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = readConfig(args);
  return {
    providerAUrl: required(values, 'providerAUrl'),
    providerBUrl: required(values, 'providerBUrl'),
    workflowUrl: required(values, 'workflowUrl'),
    directConsumerUrl: required(values, 'directConsumerUrl'),
    singleConsumerUrl: required(values, 'singleConsumerUrl'),
    backpressureConsumerUrl: required(values, 'backpressureConsumerUrl'),
    locationConsumerUrl: required(values, 'locationConsumerUrl'),
    providerMain: required(values, 'providerMain'),
    consumerMain: required(values, 'consumerMain'),
    redisEndpoint: required(values, 'redisEndpoint'),
    redisKeyPrefix: required(values, 'redisKeyPrefix'),
    logDir: required(values, 'logDir'),
    scenario: typeof values.scenario === 'string' ? values.scenario : 'all'
  };
}

function readConfig(args: readonly string[]): Record<string, unknown> {
  if (args.length !== 2 || args[0] !== '--config' || args[1].startsWith('--')) throw new Error('--config <path> is required.');
  const document = JSON.parse(fs.readFileSync(args[1], 'utf8')) as { e2e?: unknown };
  if (document.e2e === null || typeof document.e2e !== 'object' || Array.isArray(document.e2e)) throw new Error("Configuration section 'e2e' must be an object.");
  return document.e2e as Record<string, unknown>;
}

function required(values: Record<string, unknown>, key: string): string {
  const value = values[key];
  if (typeof value !== 'string' || value.length === 0) throw new Error(`Configuration value 'e2e.${key}' is required.`);
  return value;
}
import fs from 'node:fs';
