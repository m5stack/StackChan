/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package xiaozhi

type Device struct {
	DeviceID     int    `json:"device_id"`
	AgentID      int    `json:"agent_id"`
	ID           int    `json:"id"`
	ProductID    int    `json:"product_id"`
	Seed         string `json:"seed"`
	SerialNumber string `json:"serial_number"`
	ActivateAt   string `json:"activate_at"`
	ProductName  string `json:"product_name"`
	MacAddress   string `json:"mac_address"`
	AppVersion   string `json:"app_version"`
	BoardName    string `json:"board_name"`
	ClientId     string `json:"client_id"`
	IccID        string `json:"iccid"`
	Imei         string `json:"imei"`
	Online       bool   `json:"online"`
}
