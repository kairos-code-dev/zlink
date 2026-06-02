import {
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamCompression,
  ZlinkStreamConnectorOptions,
  ZlinkStreamDispatchMode,
  ZlinkStreamErrorCode,
  ZlinkStreamHeartbeatOptions,
  ZlinkStreamReconnectOptions
} from './contracts';
import { connectorError } from './support';
import { inferTransport, NodeStreamTransportFactory } from './transport';

export function normalizeOptions(options: ZlinkStreamConnectorOptions): RequiredZlinkStreamConnectorOptions {
  const endpoint = options.endpoint;
  if (endpoint.trim().length === 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint must not be empty.');
  }
  const inferredTransport = inferTransport(endpoint);
  if (options.transport !== undefined && options.transport !== inferredTransport) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Configured transport conflicts with endpoint scheme.');
  }
  validatePositive(options.connectTimeoutMs ?? 5000, 'ConnectTimeout');
  validatePositive(options.requestTimeoutMs ?? 30000, 'RequestTimeout');
  validatePositive(options.maxSendPayloadSize ?? 64 * 1024, 'MaxSendPayloadSize');
  validateHeartbeat(options.heartbeat);
  validateReconnect(options.reconnect);

  return {
    endpoint,
    transport: inferredTransport,
    connectTimeoutMs: options.connectTimeoutMs ?? 5000,
    requestTimeoutMs: options.requestTimeoutMs ?? 30000,
    heartbeat: {
      enabled: options.heartbeat?.enabled ?? true,
      intervalMs: options.heartbeat?.intervalMs ?? 1000,
      timeoutMs: options.heartbeat?.timeoutMs ?? 5000
    },
    reconnect: {
      enabled: options.reconnect?.enabled ?? true,
      initialDelayMs: options.reconnect?.initialDelayMs ?? 250,
      maxDelayMs: options.reconnect?.maxDelayMs ?? 5000,
      backoffFactor: options.reconnect?.backoffFactor ?? 2.0,
      maxAttempts: options.reconnect?.maxAttempts ?? 3
    },
    maxSendPayloadSize: options.maxSendPayloadSize ?? 64 * 1024,
    skipServerCertificateValidation: options.skipServerCertificateValidation ?? false,
    dispatchMode: options.dispatchMode ?? ZlinkStreamDispatchMode.Manual,
    compression: options.compression ?? ZlinkStreamCompression.None,
    nameResolver: options.nameResolver ?? { resolve: (type) => type.name },
    transportFactory: options.transportFactory ?? new NodeStreamTransportFactory()
  };
}


function validatePositive(value: number, name: string): void {
  if (value <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, `${name} must be positive.`);
  }
}

function validateHeartbeat(options: ZlinkStreamHeartbeatOptions | undefined): void {
  const enabled = options?.enabled ?? true;
  const intervalMs = options?.intervalMs ?? 1000;
  const timeoutMs = options?.timeoutMs ?? 5000;
  if (!enabled) {
    return;
  }
  validatePositive(intervalMs, 'Heartbeat interval');
  validatePositive(timeoutMs, 'Heartbeat timeout');
  if (timeoutMs <= intervalMs) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Heartbeat timeout must be greater than the heartbeat interval.');
  }
}

function validateReconnect(options: ZlinkStreamReconnectOptions | undefined): void {
  const enabled = options?.enabled ?? true;
  if (!enabled) {
    return;
  }
  validatePositive(options?.initialDelayMs ?? 250, 'Reconnect InitialDelay');
  validatePositive(options?.maxDelayMs ?? 5000, 'Reconnect MaxDelay');
  if ((options?.backoffFactor ?? 2.0) < 1.0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Reconnect BackoffFactor must be at least 1.0.');
  }
  if ((options?.maxAttempts ?? 3) <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Reconnect MaxAttempts must be null or positive.');
  }
}

