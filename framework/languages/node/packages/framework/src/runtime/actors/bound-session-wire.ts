export const ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET = '__zlink.actor.bound_session.send';
export const ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET = '__zlink.actor.bound_session.response';
export const ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET = '__zlink.actor.bound_session.error';
export const ZLINK_REMOTE_BOUND_SESSION_OWNERSHIP_PACKET = '__zlink.actor.bound_session.ownership';

export function decodeRemoteBoundSessionSendPayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName?: string;
  readonly metadata?: Record<string, string>;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== ZLINK_REMOTE_BOUND_SESSION_SEND_PACKET ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string'
  ) {
    throw new Error('Remote bound session send payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    message: (payload as { message?: unknown }).message,
    boundPacketName: optionalString(payload, 'boundPacketName'),
    metadata: metadataRecordOf((payload as { metadata?: unknown }).metadata)
  };
}

export function decodeRemoteBoundSessionResponsePayload(payload: unknown): {
  readonly actorId: string;
  readonly message: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly compressPayload: boolean;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_RESPONSE_PACKET);
  return { ...decoded, message: (payload as { message?: unknown }).message };
}

export function decodeRemoteBoundSessionErrorPayload(payload: unknown): {
  readonly actorId: string;
  readonly error: unknown;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly actorPacketTarget?: unknown;
} {
  const decoded = decodeRemoteBoundSessionControlPayload(payload, ZLINK_REMOTE_BOUND_SESSION_ERROR_PACKET);
  return {
    actorId: decoded.actorId,
    error: (payload as { error?: unknown }).error,
    boundPacketName: decoded.boundPacketName,
    requestSeq: decoded.requestSeq,
    metadata: decoded.metadata,
    actorPacketTarget: decoded.actorPacketTarget
  };
}

export function streamMetadataMap(metadata: unknown): ReadonlyMap<string, string> {
  if (metadata instanceof Map) {
    return new Map(metadata);
  }
  const maybeValues = metadata as { values?: unknown } | undefined;
  if (maybeValues?.values instanceof Map) {
    return new Map(maybeValues.values);
  }
  return new Map();
}

function decodeRemoteBoundSessionControlPayload(payload: unknown, packetName: string): {
  readonly actorId: string;
  readonly boundPacketName: string;
  readonly requestSeq: bigint;
  readonly metadata?: Record<string, string>;
  readonly compressPayload: boolean;
  readonly actorPacketTarget?: unknown;
} {
  if (
    typeof payload !== 'object' ||
    payload === null ||
    (payload as { packetName?: unknown }).packetName !== packetName ||
    typeof (payload as { actorId?: unknown }).actorId !== 'string' ||
    typeof (payload as { boundPacketName?: unknown }).boundPacketName !== 'string' ||
    typeof (payload as { requestSeq?: unknown }).requestSeq !== 'string'
  ) {
    throw new Error('Remote bound session control payload is invalid.');
  }
  return {
    actorId: (payload as { actorId: string }).actorId,
    boundPacketName: (payload as { boundPacketName: string }).boundPacketName,
    requestSeq: BigInt((payload as { requestSeq: string }).requestSeq),
    metadata: metadataRecordOf((payload as { metadata?: unknown }).metadata),
    compressPayload: (payload as { compressPayload?: unknown }).compressPayload === true,
    actorPacketTarget: (payload as { actorPacketTarget?: unknown }).actorPacketTarget
  };
}

function metadataRecordOf(rawMetadata: unknown): Record<string, string> {
  const metadata: Record<string, string> = {};
  if (typeof rawMetadata === 'object' && rawMetadata !== null) {
    for (const [key, value] of Object.entries(rawMetadata)) {
      if (typeof value === 'string') {
        metadata[key] = value;
      }
    }
  }
  return metadata;
}

function optionalString(value: object, key: string): string | undefined {
  const field = (value as Record<string, unknown>)[key];
  return typeof field === 'string' ? field : undefined;
}
