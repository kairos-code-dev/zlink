export interface ClientOptions {
  readonly publisherUrl: string;
  readonly lateSubscriberUrl: string;
  readonly publisherEndpoint: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly publisherMain: string;
  readonly subscriberMain: string;
  readonly logDir: string;
  readonly scenario: string;
  readonly subscriberUrls: readonly string[];
}

export function parseClientOptions(args: readonly string[]): ClientOptions {
  const values = readConfig(args);

  return {
    publisherUrl: required(values, 'publisherUrl'),
    lateSubscriberUrl: required(values, 'lateSubscriberUrl'),
    publisherEndpoint: required(values, 'publisherEndpoint'),
    redisEndpoint: required(values, 'redisEndpoint'),
    redisKeyPrefix: required(values, 'redisKeyPrefix'),
    publisherMain: required(values, 'publisherMain'),
    subscriberMain: required(values, 'subscriberMain'),
    logDir: required(values, 'logDir'),
    scenario: optional(values, 'scenario', 'all'),
    subscriberUrls: many(values, 'subscriberUrls')
  };
}

function readConfig(args: readonly string[]): Record<string, unknown> {
  if (args.length !== 2 || args[0] !== '--config' || args[1].startsWith('--')) throw new Error('--config <path> is required.');
  const document = JSON.parse(fs.readFileSync(args[1], 'utf8')) as { e2e?: unknown };
  if (document.e2e === null || typeof document.e2e !== 'object' || Array.isArray(document.e2e)) throw new Error("Configuration section 'e2e' must be an object.");
  return document.e2e as Record<string, unknown>;
}

function required(values: Record<string, unknown>, name: string): string {
  const value = values[name];
  if (typeof value !== 'string' || value.length === 0) throw new Error(`Configuration value 'e2e.${name}' is required.`);
  return value;
}

function optional(values: Record<string, unknown>, name: string, fallback: string): string {
  const value = values[name];
  return typeof value === 'string' ? value : fallback;
}

function many(values: Record<string, unknown>, name: string): readonly string[] {
  const value = values[name];
  if (!Array.isArray(value) || value.length === 0 || value.some((entry) => typeof entry !== 'string')) throw new Error(`Configuration value 'e2e.${name}' must be a string array.`);
  return value as string[];
}
import fs from 'node:fs';
