import { parseArgs, required } from '../../LocationProbe/Configuration/location-probe-options';

export interface ConsumerOptions {
  readonly httpUrl: string;
  readonly logDir: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly traceLabel: string;
}

export function parseConsumerOptions(args: readonly string[]): ConsumerOptions {
  const values = parseArgs(args);
  return {
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    traceLabel: values.get('trace-label') ?? 'consumer'
  };
}
