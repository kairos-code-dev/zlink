// SPDX-License-Identifier: MPL-2.0

export const AutoConnectType = Object.freeze({
  Invalid: 0, RouteMesh: 1, ClientServer: 2, DealerMesh: 3, Fanout: 4, SpotMesh: 5
} as const);
export type AutoConnectType = typeof AutoConnectType[keyof typeof AutoConnectType];

export const ServiceRole = Object.freeze({
  Invalid: 0, Spot: 2, Router: 3, Dealer: 4, Pub: 5, Sub: 6
} as const);
export type ServiceRoleValue = typeof ServiceRole[keyof typeof ServiceRole];

export const ServiceKind = Object.freeze({
  Discovery: 1, SpotSub: 3, SpotPub: 4, Socket: 5
} as const);
export type ServiceKindValue = typeof ServiceKind[keyof typeof ServiceKind];
