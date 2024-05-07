/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Intel Corporation.
 *   All rights reserved.
 */

package client

import (
	"encoding/json"
	"fmt"
	"log"
	"reflect"

	"github.com/spdk/spdk/go/rpc/generators/utils"
)

type SPDKClient struct {
	rpcClient IClient
}

const (
	spdkSocketAddress = "/var/tmp/spdk.sock"
)

// Call method sends a JSON-RPC 2.0 request using rpc struct.
func (c *SPDKClient) Call(methodStruct any) (*Response, error) {
	value := reflect.ValueOf(methodStruct)
	if value.Kind() == reflect.Struct || value.Elem().Kind() == reflect.Struct {
		return c.rpcClient.Call(prepareRequestFromStruct(methodStruct))
	}

	return nil, &Error{Message: "Wrong type of provided argument."}
}

func prepareRequestFromStruct(methodStruct any) (string, map[string]any) {
	var methodName string
	t := reflect.TypeOf(methodStruct)

	if t.Kind() == reflect.Ptr {
		methodName = t.Elem().Name()
	} else {
		methodName = t.Name()
	}

	// extract params using marshal to get json encoding
	jsonBody, err := json.Marshal(methodStruct)
	if err != nil {
		log.Print(fmt.Errorf("error when creating json: %w", err).Error())
	}

	var params map[string]any
	// parsing the json data to store the result in the params map
	err = json.Unmarshal(jsonBody, &params)
	if err != nil {
		log.Print(fmt.Errorf("error during parsing struct values to params map: %w", err).
			Error())
	}

	return utils.CamelCaseToSnakeCase(methodName), params
}

// CreateSPDKClient creates a new JSON-RPC client.
func CreateSPDKClient() (*SPDKClient, error) {
	client, err := CreateClientWithJsonCodec(Unix, spdkSocketAddress)
	if err != nil {
		log.Printf("error on SPDK client creation, err: %s", err.Error())
		return nil, err
	}
	return &SPDKClient{rpcClient: client}, nil
}

// Close closes connection with underlying stream.
func (c *SPDKClient) Close() error {
	return c.rpcClient.Close()
}
