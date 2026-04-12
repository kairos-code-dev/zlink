package main

import (
	"bytes"
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

	endpoint := samplecommon.UniqueTCP("pair-recv")
	samplecommon.Must(server.Bind(endpoint))
	samplecommon.Must(client.Connect(endpoint))
	samplecommon.WaitConnected(serverMon, clientMon)

	sent := "hello-pair"
	samplecommon.Must(client.Send(zlink.SendFlagsNone, samplecommon.Message(sent)))

	received, err := server.Recv(zlink.RecvFlagsNone)
	samplecommon.Must(err)
	defer received.Close()
	part, err := received.SinglePartOrError()
	samplecommon.Must(err)
	if !bytes.Equal(part.Data(), []byte(sent)) {
		samplecommon.Must(fmt.Errorf("unexpected payload %q", string(part.Data())))
	}

	fmt.Printf("[pair/recv] send: %q -> recv: %q\n", sent, string(part.Data()))
}
