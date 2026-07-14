export interface ServerOptions {
  readonly role: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly evidenceFile?: string;
  readonly rid: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
}

export function validateServerOptions(value: unknown, defaultRole = 'location-probe'): ServerOptions {
  const values = objectValues(value);
  const rid = optionalString(values, 'rid') ?? 'node';
  const redisEndpoint = optionalString(values, 'redisEndpoint');
  const redisKeyPrefix = optionalString(values, 'redisKeyPrefix');
  if (redisEndpoint === undefined) {
    throw new Error('--redis-endpoint is required.');
  }
  if (redisKeyPrefix === undefined) {
    throw new Error('--redis-key-prefix is required.');
  }
  return {
    role: defaultRole,
    httpUrl: optionalString(values, 'httpUrl') ?? 'http://127.0.0.1:0',
    logDir: optionalString(values, 'logDir') ?? '/tmp/zlink-node-e2e-log',
    evidenceFile: optionalString(values, 'evidenceFile'),
    rid,
    redisEndpoint,
    redisKeyPrefix
  };
}
import { objectValues, optionalString } from '../../../configuration';
