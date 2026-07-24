export type Type<T = unknown> = new (...args: never[]) => T;
export type RoutingId = string;
/** Global logical Spot identity. It is not a transport RoutingId. */
export type SpotId = string;
