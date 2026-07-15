export interface SessionOptions {
  readonly rid: string;
  readonly httpUrl: string;
  readonly controlRouterEndpoint: string;
  readonly playControlEndpoints: readonly string[];
  readonly spotRouterEndpoint: string;
  readonly playSpotRouterEndpoints: ReadonlyMap<string, string>;
  readonly streamEndpoint: string;
  readonly tlsStreamEndpoint?: string;
  readonly tlsCertPath?: string;
  readonly tlsKeyPath?: string;
  readonly evidenceFile?: string;
  readonly logDir: string;
}

export function parseSessionOptions(args: readonly string[]): SessionOptions {
  const values = new Map<string, string>();
  for (let i = 0; i < args.length; i += 1) {
    const key = args[i];
    if (!key.startsWith('--')) {
      continue;
    }
    if (i + 1 >= args.length) {
      throw new Error(`Missing value for ${key}.`);
    }
    values.set(key.slice(2), args[++i]);
  }
  const rid = required(values, 'rid');
  return {
    rid,
    httpUrl: required(values, 'http-url'),
    controlRouterEndpoint: required(values, 'control-router-endpoint'),
    playControlEndpoints: requiredList(values, 'play-control-endpoint'),
    spotRouterEndpoint: required(values, 'spot-router-endpoint'),
    playSpotRouterEndpoints: requiredPeerMap(values, 'play-spot-router'),
    streamEndpoint: required(values, 'stream-endpoint'),
    tlsStreamEndpoint: values.get('tls-stream-endpoint'),
    tlsCertPath: values.get('tls-cert-path'),
    tlsKeyPath: values.get('tls-key-path'),
    evidenceFile: values.get('evidence-file'),
    logDir: required(values, 'log-dir')
  };
}

function requiredList(values: ReadonlyMap<string, string>, key: string): readonly string[] {
  return required(values, key).split(',').map((value) => value.trim()).filter((value) => value.length > 0);
}

function requiredPeerMap(values: ReadonlyMap<string, string>, keyPrefix: string): ReadonlyMap<string, string> {
  const result = new Map<string, string>();
  for (const [key, value] of values) {
    if (!key.startsWith(`${keyPrefix}-`)) {
      continue;
    }
    const peerRid = key.slice(keyPrefix.length + 1);
    if (peerRid.length > 0 && value.length > 0) {
      result.set(peerRid, value);
    }
  }
  if (result.size === 0) {
    throw new Error(`--${keyPrefix}-<rid> is required.`);
  }
  return result;
}

function required(values: ReadonlyMap<string, string>, key: string): string {
  const value = values.get(key);
  if (value === undefined || value.length === 0) {
    throw new Error(`--${key} is required.`);
  }
  return value;
}
