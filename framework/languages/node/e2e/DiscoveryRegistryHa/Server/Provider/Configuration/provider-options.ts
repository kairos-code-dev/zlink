import { parseArgs, required } from '../../LocationProbe/Configuration/location-probe-options';

export interface ProviderOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
  readonly channelEndpoint: string;
  readonly evidenceFile?: string;
}

export function parseProviderOptions(args: readonly string[]): ProviderOptions {
  const values = parseArgs(args);
  return {
    rid: values.get('rid') ?? 'api-a',
    httpUrl: values.get('http-url') ?? 'http://127.0.0.1:0',
    logDir: values.get('log-dir') ?? 'logs',
    redisEndpoint: required(values, 'redis-endpoint'),
    redisKeyPrefix: required(values, 'redis-key-prefix'),
    channelEndpoint: required(values, 'channel-endpoint'),
    evidenceFile: values.get('evidence-file')
  };
}
