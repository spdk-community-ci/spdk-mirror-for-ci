/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2023 Intel Corporation.
 *   All rights reserved.
 */

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/spdk/spdk/go/rpc/client"
	"github.com/spdk/spdk/go/rpc/generators/utils"
)

const (
	defaultBdevName = ""
	defaultTimeout  = 0
)

func main() {

	//create client
	spdkClient, err := client.CreateSPDKClient()
	if err != nil {
		log.Fatalf("error on client creation, err: %s", err.Error())
	}
	defer spdkClient.Close()

	bdevGetBdevStruct := &utils.BdevGetBdevs{}
	fillBdevGetBdevStructWithParams(bdevGetBdevStruct)

	//sends a JSON-RPC 2.0 request with "bdev_get_bdevs" method and provided params
	resp, err := spdkClient.Call(bdevGetBdevStruct)
	if err != nil {
		log.Fatalf("error on JSON-RPC call, err: %s", err.Error())
	}

	result, err := json.Marshal(resp.Result)
	if err != nil {
		log.Print(fmt.Errorf("error when creating json string representation: %w", err).Error())
	}

	fmt.Printf("%s\n", string(result))
}

// convert command line arg to bdevGetBdevs struct params
func fillBdevGetBdevStructWithParams(bdevStruct *utils.BdevGetBdevs) {
	fs := flag.NewFlagSet("set", flag.ContinueOnError)
	name := fs.String("Name", defaultBdevName, "Name of the Blockdev")
	timeout := fs.Int64("Timeout", defaultTimeout, "Time in ms to wait for the bdev to appear")

	err := fs.Parse(os.Args[1:])
	if err != nil {
		log.Fatalf("%s\n", err.Error())
	}

	bdevStruct.Name = *name
	bdevStruct.Timeout = *timeout
}
