// SPDX-License-Identifier: MPL-2.0

export const SpotNodeOption = Object.freeze({
  ROUTER_HWM_PROFILE: 0x360E,
  ROUTER_HWM: 0x360F,
  PUBSUB_HWM_PROFILE: 0x3610,
  PUBSUB_HWM: 0x3611,
  DISPATCH_WORKERS_MIN: 0x3612,
  DISPATCH_WORKERS_MAX: 0x3613
} as const);

export const SpotOption = Object.freeze({
  REQUEST_TIMEOUT_MS: 0x3701
} as const);
