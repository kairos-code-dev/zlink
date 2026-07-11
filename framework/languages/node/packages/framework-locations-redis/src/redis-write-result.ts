import {
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkLocationWriteResult
} from '@zlink-systems/framework';
import { asString, toNumber } from './redis-values';

export function intentName(intent: ZLinkLocationWriteIntent): string {
  switch (intent) {
    case ZLinkLocationWriteIntent.NewClaim:
      return 'new';
    case ZLinkLocationWriteIntent.Renew:
      return 'renew';
    case ZLinkLocationWriteIntent.Takeover:
      return 'takeover';
    default:
      throw new RangeError(`Unknown location write intent: ${intent}`);
  }
}

export function toWriteResult(result: readonly unknown[]): ZLinkLocationWriteResult {
  const status = asString(result[0]);
  if (status === 'stored') {
    return stored(BigInt(asString(result[1])), fromUnixMs(toNumber(result[2])));
  }
  if (status === 'conflict') {
    return rejectedConflict();
  }
  return ignoredStale();
}

export function fromUnixMs(value: number): Date {
  return new Date(value);
}

function stored(generation: bigint, updatedAt: Date): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.Stored, generation, updatedAt };
}

function ignoredStale(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.IgnoredStale, generation: 0n, updatedAt: new Date(0) };
}

function rejectedConflict(): ZLinkLocationWriteResult {
  return { status: ZLinkLocationWriteStatus.RejectedConflict, generation: 0n, updatedAt: new Date(0) };
}
