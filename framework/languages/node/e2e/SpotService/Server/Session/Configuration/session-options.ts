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

export function validateSessionOptions(value: unknown): SessionOptions {
  const values = objectValues(value);
  const peers = stringList(values, 'playSpotRouterEndpoints');
  const peerMap = new Map<string, string>();
  for (const peer of peers) {
    const separator = peer.indexOf('=');
    if (separator <= 0 || separator === peer.length - 1) {
      throw new Error("Configuration value 'e2e.playSpotRouterEndpoints' entries must use '<rid>=<endpoint>'.");
    }
    peerMap.set(peer.slice(0, separator), peer.slice(separator + 1));
  }
  if (peerMap.size === 0) {
    throw new Error("Configuration value 'e2e.playSpotRouterEndpoints' must contain at least one peer.");
  }
  return {
    rid: requiredString(values, 'rid'),
    httpUrl: requiredString(values, 'httpUrl'),
    controlRouterEndpoint: requiredString(values, 'controlRouterEndpoint'),
    playControlEndpoints: stringList(values, 'playControlEndpoints'),
    spotRouterEndpoint: requiredString(values, 'spotRouterEndpoint'),
    playSpotRouterEndpoints: peerMap,
    streamEndpoint: requiredString(values, 'streamEndpoint'),
    tlsStreamEndpoint: optionalString(values, 'tlsStreamEndpoint'),
    tlsCertPath: optionalString(values, 'tlsCertPath'),
    tlsKeyPath: optionalString(values, 'tlsKeyPath'),
    evidenceFile: optionalString(values, 'evidenceFile'),
    logDir: requiredString(values, 'logDir')
  };
}
import { objectValues, optionalString, requiredString, stringList } from '../../../configuration';
