package systems.zlink.framework.locations;

public sealed interface ZLinkLocationKey
    permits ZLinkLocationKey.Peer,
            ZLinkLocationKey.Spot,
            ZLinkLocationKey.Actor,
            ZLinkLocationKey.Route {
    ZLinkLocationKind kind();

    record Peer(ZLinkPeerLocationKey key) implements ZLinkLocationKey {
        @Override
        public ZLinkLocationKind kind() {
            return ZLinkLocationKind.PEER;
        }
    }

    record Spot(ZLinkSpotLocationKey key) implements ZLinkLocationKey {
        @Override
        public ZLinkLocationKind kind() {
            return ZLinkLocationKind.SPOT;
        }
    }

    record Actor(ZLinkActorLocationKey key) implements ZLinkLocationKey {
        @Override
        public ZLinkLocationKind kind() {
            return ZLinkLocationKind.ACTOR;
        }
    }

    record Route(ZLinkRouteLocationKey key) implements ZLinkLocationKey {
        @Override
        public ZLinkLocationKind kind() {
            return ZLinkLocationKind.ROUTE;
        }
    }
}
