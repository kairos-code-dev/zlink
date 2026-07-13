package systems.zlink.framework.runtime.locations;

interface ZLinkAutoConnectExecutor {
    void connect(ZLinkAutoConnectPlanner.Target target);

    void disconnect(ZLinkAutoConnectPlanner.Target target);

    default boolean isManual(ZLinkAutoConnectPlanner.Target target) {
        return false;
    }

    default void replace(
        ZLinkAutoConnectPlanner.Target current,
        ZLinkAutoConnectPlanner.Target replacement) {
        disconnect(current);
        connect(replacement);
    }

    ZLinkAutoConnectExecutor NONE = new ZLinkAutoConnectExecutor() {
        @Override
        public void connect(ZLinkAutoConnectPlanner.Target target) {
        }

        @Override
        public void disconnect(ZLinkAutoConnectPlanner.Target target) {
        }
    };
}
