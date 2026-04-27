/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

package model

type User struct {
	Uid            int64  `json:"uid"            orm:"uid"             description:"User unique UID (remote platform primary key)"` // User unique UID (remote platform primary key)
	Username       string `json:"username"       orm:"username"        description:"Login username"`                                // Login username
	Userslug       string `json:"userslug"       orm:"userslug"        description:"User alias"`                                    // User alias
	DisplayName    string `json:"displayName"    orm:"display_name"    description:"User display name"`                             // User display name
	IconText       string `json:"iconText"       orm:"icon_text"       description:"User icon text"`                                // User icon text
	IconBgColor    string `json:"iconBgColor"    orm:"icon_bg_color"   description:"Icon background color"`                         // Icon background color
	EmailConfirmed int    `json:"emailConfirmed" orm:"email_confirmed" description:"Email verified, 0-no 1-yes"`                    // Email verified, 0-no 1-yes
	JoinDate       int64  `json:"joinDate"       orm:"join_date"       description:"Registration timestamp (milliseconds)"`         // Registration timestamp (milliseconds)
	LastOnline     int64  `json:"lastOnline"     orm:"last_online"     description:"Last online timestamp (milliseconds)"`          // Last online timestamp (milliseconds)
	UserStatus     string `json:"userStatus"     orm:"user_status"     description:"User online status"`                            // User online status
}

type RemoteLoginResp struct {
	Status struct {
		Code    string `json:"code"`
		Message string `json:"message"`
	} `json:"status"`
	Response struct {
		Uid            int64       `json:"uid"`
		Username       string      `json:"username"`
		Userslug       string      `json:"userslug"`
		Picture        interface{} `json:"picture"`
		Status         string      `json:"status"`
		Postcount      int         `json:"postcount"`
		Reputation     int         `json:"reputation"`
		EmailConfirmed int         `json:"email:confirmed"`
		Lastonline     int64       `json:"lastonline"`
		Flags          interface{} `json:"flags"`
		Banned         bool        `json:"banned"`
		BannedExpire   int         `json:"banned:expire"`
		Joindate       int64       `json:"joindate"`
		Fullname       interface{} `json:"fullname"`
		Displayname    string      `json:"displayname"`
		IconText       string      `json:"icon:text"`
		IconBgColor    string      `json:"icon:bgColor"`
		JoindateISO    string      `json:"joindateISO"`
		LastonlineISO  string      `json:"lastonlineISO"`
		BannedUntil    int         `json:"banned_until"`
		BannedReadable string      `json:"banned_until_readable"`
	} `json:"response"`
}

type RegistrationResponse struct {
	Uid            int64       `json:"uid"`
	Username       string      `json:"username"`
	Userslug       string      `json:"userslug"`
	Email          string      `json:"email"`
	EmailConfirmed int         `json:"email:confirmed"`
	JoinDate       int64       `json:"joindate"`
	LastOnline     int64       `json:"lastonline"`
	Picture        interface{} `json:"picture"`
	IconBgColor    string      `json:"icon:bgColor"`
	Fullname       interface{} `json:"fullname"`
	Displayname    string      `json:"displayname"`
	IconText       string      `json:"icon:text"`
	UserStatus     string      `json:"status"` // User status: online
}

type RemoteRegisterResp struct {
	Status struct {
		Code    string `json:"code"`    // Status code: ok/bad-request
		Message string `json:"message"` // Error message / success message
	} `json:"status"`
	RegistrationResponse `json:"response"`
}
