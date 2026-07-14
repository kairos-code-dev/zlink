import { objectValues, optionalString } from '../../../configuration';

export interface ServerOptions {
  readonly role: string;
  readonly httpUrl: string;
  readonly logDir: string;
  readonly evidenceFile?: string;
  readonly rid: string;
  readonly redisEndpoint: string;
  readonly redisKeyPrefix: string;
}

export function validateServerOptions(value: unknown, defaultRole = 'topology-probe'): ServerOptions {
  const values = objectValues(value);
  const redisEndpoint = optionalString(values, 'redisEndpoint');
  const redisKeyPrefix = optionalString(values, 'redisKeyPrefix');
  if (!redisEndpoint || !redisKeyPrefix) throw new Error("Configuration requires 'e2e.redisEndpoint' and 'e2e.redisKeyPrefix'.");
  return {
    role: defaultRole,
    httpUrl: optionalString(values, 'httpUrl') ?? 'http://127.0.0.1:0',
    logDir: optionalString(values, 'logDir') ?? '/tmp/zlink-node-e2e-log',
    evidenceFile: optionalString(values, 'evidenceFile'),
    rid: optionalString(values, 'rid') ?? 'node',
    redisEndpoint,
    redisKeyPrefix
  };
}
