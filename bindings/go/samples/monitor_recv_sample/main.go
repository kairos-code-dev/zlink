package main

import (
	"fmt"
	"zlink"
	"zlink/samples/internal/samplecommon"
)

func main() {
	ctx, err := zlink.NewContext()
	samplecommon.Must(err)
	defer ctx.Close()

	server, err := ctx.PairSocket()
	samplecommon.Must(err)
	defer server.Close()
	client, err := ctx.PairSocket()
	samplecommon.Must(err)
	defer client.Close()

	serverMon := samplecommon.OpenMonitor(server)
	defer serverMon.Close()
	clientMon := samplecommon.OpenMonitor(client)
	defer clientMon.Close()
	if event, ok, err := serverMon.TryRecv(); err != nil || ok || event != nil {
		samplecommon.Must(fmt.Errorf("monitor sample expected empty server TryRecv"))
	}
	if event, ok, err := clientMon.TryRecv(); err != nil || ok || event != nil {
		samplecommon.Must(fmt.Errorf("monitor sample expected empty client TryRecv"))
	}

	endpoint := samplecommon.UniqueTCP("monitor")
	samplecommon.Must(server.Bind(endpoint))
	samplecommon.Must(client.Connect(endpoint))

	serverEvent := samplecommon.WaitMonitorEvent(serverMon)
	clientEvent := samplecommon.WaitMonitorEvent(clientMon)
	if !serverEvent.IsConnectionReadyChanged() || !clientEvent.IsConnectionReadyChanged() {
		samplecommon.Must(fmt.Errorf("monitor sample expected CONNECTION_READY_CHANGED"))
	}
	fmt.Println("[monitor/recv] recv: \"connection-ready\" -> tryRecv: empty")
}
