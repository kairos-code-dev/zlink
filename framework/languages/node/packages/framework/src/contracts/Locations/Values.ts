export enum ZLinkLocationAutoConnectType {
  Invalid = 0,
  RouteMesh = 1,
  ClientServer = 2,
  DealerMesh = 3,
  Fanout = 4,
  SpotMesh = 5
}

export enum ZLinkLocationRole {
  Invalid = 0,
  // Value 1 is reserved for the removed gateway role. The numeric values are
  // serialized on the wire and in Redis row JSON, so they must not change.
  Spot = 2,
  Router = 3,
  Dealer = 4,
  Pub = 5,
  Sub = 6
}

export enum ZLinkRouteKind {
  Invalid = 0,
  ActorSession = 1,
  SpotName = 2,
  FrameworkRoute = 3
}

export enum ZLinkLocationKind {
  Invalid = 0,
  Peer = 1,
  Spot = 2,
  Actor = 3,
  Route = 4
}
