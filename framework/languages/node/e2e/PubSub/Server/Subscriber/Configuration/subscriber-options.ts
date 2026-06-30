import { parseArgs, required } from '../../Registry/Configuration/server-options';

export const SUBSCRIBER_OPTIONS = Symbol.for('@zlink-systems/e2e-pubsub:subscriber-options');

export interface SubscriberOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly registryRouterEndpoint: string;
  readonly publisherEndpoint: string;
  readonly evidenceFile?: string;
  readonly handlerDelayMs: number;
}

export function parseSubscriberOptions(args: readonly string[]): SubscriberOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? 'subscriber',
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    registryRouterEndpoint: required(values, 'registry-router-endpoint'),
    publisherEndpoint: required(values, 'publisher-endpoint'),
    evidenceFile: values.get('evidence-file'),
    handlerDelayMs: Number.parseInt(values.get('handler-delay-ms') ?? '0', 10)
  };
}
