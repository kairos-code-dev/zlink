export interface ClientOptions {
  readonly sessionAStreamEndpoint: string;
  readonly sessionBStreamEndpoint: string;
  readonly scenario: string;
  readonly requestId?: string;
  readonly spotRid?: string;
}

export function parseClientOptions(value: unknown): ClientOptions {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) throw new Error('Client configuration must be an object.');
  const values = value as Record<string, unknown>;
  return {
    sessionAStreamEndpoint: required(values, 'sessionAStreamEndpoint'),
    sessionBStreamEndpoint: required(values, 'sessionBStreamEndpoint'),
    scenario: optional(values, 'scenario') ?? 'full',
    requestId: optional(values, 'requestId'),
    spotRid: optional(values, 'spotRid')
  };
}

function required(values: Record<string, unknown>, key: string): string {
  const value = values[key];
  if (typeof value !== 'string' || value.length === 0) throw new Error(`Configuration value '${key}' is required.`);
  return value;
}

function optional(values: Record<string, unknown>, key: string): string | undefined {
  const value = values[key];
  if (value === undefined) return undefined;
  if (typeof value !== 'string') throw new Error(`Configuration value '${key}' must be a string.`);
  return value;
}
