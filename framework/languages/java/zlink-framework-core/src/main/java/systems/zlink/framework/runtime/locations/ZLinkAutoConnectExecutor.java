package systems.zlink.framework.runtime.locations;

interface ZLinkAutoConnectExecutor {
    void connect(ZLinkAutoConnectPlanner.Target target);

    void disconnect(ZLinkAutoConnectPlanner.Target target);

    ZLinkAutoConnectExecutor NONE = new ZLinkAutoConnectExecutor() {
        @Override
        public void connect(ZLinkAutoConnectPlanner.Target target) {
        }

        @Override
        public void disconnect(ZLinkAutoConnectPlanner.Target target) {
        }
    };
}
