import { objectValues, optional, required } from '../../Shared/Configuration/server-options';

export const SUBSCRIBER_OPTIONS = Symbol.for('@zlink-systems/e2e-pubsub:subscriber-options');

export interface SubscriberOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly evidenceFile?: string;
  readonly handlerDelayMs: number;
  readonly publisherEndpoint?: string;
}

export function validateSubscriberOptions(value: unknown): SubscriberOptions {
  const values = objectValues(value);
  const handlerDelayMs = values.handlerDelayMs === undefined ? 0 : Number(values.handlerDelayMs);
  if (!Number.isFinite(handlerDelayMs) || handlerDelayMs < 0) throw new Error("Configuration value 'e2e.handlerDelayMs' must be a non-negative number.");
  return {
    rid: optional(values, 'rid') ?? 'subscriber',
    httpUrl: optional(values, 'httpUrl') ?? 'http://127.0.0.1:0',
    logDir: optional(values, 'logDir') ?? 'logs',
    redisEndpoint: required(values, 'redisEndpoint'),
    redisKeyPrefix: required(values, 'redisKeyPrefix'),
    evidenceFile: optional(values, 'evidenceFile'),
    handlerDelayMs,
    publisherEndpoint: optional(values, 'publisherEndpoint')
  };
}
