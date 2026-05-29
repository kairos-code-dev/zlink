// SPDX-License-Identifier: MPL-2.0

export const SocketType = Object.freeze({
  ANY: 0,
  PAIR: 0x1001, PUB: 0x1002, SUB: 0x1003, DEALER: 0x1004,
  ROUTER: 0x1005, XPUB: 0x1006, XSUB: 0x1007, STREAM: 0x1008,
  Any: 0,
  Pair: 0x1001,
  Pub: 0x1002,
  Sub: 0x1003,
  Dealer: 0x1004,
  Router: 0x1005,
  XPub: 0x1006,
  XSub: 0x1007,
  Stream: 0x1008
} as const);
export type SocketTypeValue = typeof SocketType[keyof typeof SocketType];

export type MonitorEventMask = number;
export const SOCKET_MONITOR_EVENT_ALL = 0xFFFF;

export const SendFlags = Object.freeze({ None: 0, DontWait: 0x0001 } as const);
export type SendFlags = typeof SendFlags[keyof typeof SendFlags];

export const RecvFlags = Object.freeze({ None: 0, DontWait: 0x0001 } as const);
export type RecvFlags = typeof RecvFlags[keyof typeof RecvFlags];

export const RidDuplicatePolicy = Object.freeze({
  Reject: 0,
  Handover: 1
} as const);
export type RidDuplicatePolicy =
  typeof RidDuplicatePolicy[keyof typeof RidDuplicatePolicy];
export type RidDuplicatePolicyValue = RidDuplicatePolicy;

export const SubmitRetryMode = Object.freeze({
  Off: 0,
  LocalFailure: 1
} as const);
export type SubmitRetryMode =
  typeof SubmitRetryMode[keyof typeof SubmitRetryMode];
export type SubmitRetryModeValue = SubmitRetryMode;

export const PollEventFlag = Object.freeze({
  PollIn: 1,
  PollOut: 2,
  PollErr: 4,
  PollPri: 8,
  PollCompletion: 32
} as const);
export type PollEventFlagValue =
  typeof PollEventFlag[keyof typeof PollEventFlag];
