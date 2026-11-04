# StackChan Server

StackChan Server is the backend service for the StackChan ecosystem. It serves StackChan devices, the mobile app, the web-based admin console, and selected third-party integrations from a single Go service.

This repository already includes:

- Go backend source code
- MySQL schema initialization SQL
- Prebuilt Flutter Web admin assets
- Docker and Kustomize deployment templates
- WebSocket forwarding between StackChan devices and app clients
- XiaoZhi integration logic

Security note: this README intentionally uses placeholders such as `<your-server-host>`, `<admin-token>`, and `<your-secret>` instead of real hostnames, production addresses, keys, or tokens.

## Table of Contents

- [1. Overview](#1-overview)
- [2. Architecture and Modules](#2-architecture-and-modules)
- [3. Requirements](#3-requirements)
- [4. Quick Start from Scratch](#4-quick-start-from-scratch)
- [5. Configuration Guide](#5-configuration-guide)
- [6. Admin Console Guide](#6-admin-console-guide)
- [7. App Workflow](#7-app-workflow)
- [8. Device Workflow](#8-device-workflow)
- [9. Authentication and Response Format](#9-authentication-and-response-format)
- [10. API Reference by Module](#10-api-reference-by-module)
- [11. WebSocket Protocol](#11-websocket-protocol)
- [12. Database Overview](#12-database-overview)
- [13. Project Structure](#13-project-structure)
- [14. Development, Build, and Deployment](#14-development-build-and-deployment)
- [15. Operational Notes](#15-operational-notes)
- [16. License](#16-license)

## 1. Overview

StackChan Server covers four main responsibilities:

1. User identity
   - The app delegates registration and login to a remote M5Stack-compatible user system.
   - After a successful login, this service stores the user profile locally and issues its own JWT.

2. Device and content management
   - Devices can be created automatically, bound to users, unbound, renamed, and queried.
   - Dances, panoramic images, posts, comments, and friend relationships are stored in MySQL.

3. Admin console features
   - Admin login
   - App Store item management
   - File upload for icons, firmware, media, and other assets

4. Real-time communication
   - App clients and StackChan devices connect through `/stackChan/ws`.
   - The server forwards audio, image, motion-control, call-state, and status messages.

## 2. Architecture and Modules

### 2.1 Actors

The system has four major client types:

- Admin users: log in to the web console and manage App Store content
- App users: register, log in, bind devices, manage dances, and read device data
- StackChan devices: upload data, fetch content, and establish WebSocket sessions
- XiaoZhi platform: used for token retrieval, device unbind flow, and agent reset operations

### 2.2 Route Groups

| Route Prefix | Client | Purpose |
| --- | --- | --- |
| `/admin/stackChan` | Admin console | Admin login, upload, App Store management |
| `/stackChan/v2` | Mobile app | User login, registration, device binding, dance management |
| `/stackChan` | Device / legacy client | Device APIs, posts, pano, uploads, public app list, XiaoZhi APIs |
| `/stackChan/ws` | App / device | WebSocket real-time channel |
| `/file/*` | All clients | Static file access for uploaded files and built-in media |
| `/` | Browser | Flutter Web admin console |

### 2.3 Core Modules

- User module: remote login/registration plus local JWT issuance
- Device module: device bootstrap, bind/unbind, device info queries
- Dance module: motion JSON, dance name, and music URL management
- Community module: posts and comments from devices
- Pano module: panoramic image records per device
- App Store module: admin-side CRUD, public read access for app/device clients
- File module: upload and serve files under `/file`
- WebSocket module: connection pools, message forwarding, heartbeat, stale-connection cleanup
- XiaoZhi module: token retrieval, forced refresh, and device-side reset support

## 3. Requirements

### 3.1 Runtime Dependencies

- Go: `go 1.26.3` as declared in `go.mod`
- MySQL: recommended `8.0+`
- OS: macOS, Linux, or Windows for development; Linux recommended for production

### 3.2 Optional Tooling

- GoFrame CLI (`gf`) for `make build`, `make dao`, and `make ctrl`
- Docker for image packaging
- `kubectl` and `kustomize` for Kubernetes deployment

## 4. Quick Start from Scratch

### 4.1 Clone the Repository

```bash
git clone <your-repo-url>
cd stackchan-server
```

### 4.2 Create the Database

The SQL file uses the `stackChan.` schema prefix, so create the database first:

```sql
CREATE DATABASE IF NOT EXISTS stackChan
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;
```

Then import the schema:

```bash
mysql -u <db_user> -p < check_list/create_mysql_database.sql
```

If you want to use another database name, update all of the following:

- `check_list/create_mysql_database.sql`
- `manifest/config/config.yaml`
- `hack/config.yaml`

### 4.3 Configure the Service

Primary config file:

- `manifest/config/config.yaml`

At minimum, review these sections:

- `server.address`
- `database.default.link`
- `jwt.secret`
- `admin.users`
- `m5stack.*`
- `rsa.server.*`
- `xiaozhi.*`

Minimal example with placeholders only:

```yaml
server:
  address: ":12800"
  openapiPath: "/api.json"

logger:
  path: "./logs"
  file: "{Y-m-d}.log"
  level: "all"
  stdout: true

database:
  default:
    link: "mysql:<db_user>:<db_password>@tcp(<db_host>:3306)/stackChan?charset=utf8mb4&collation=utf8mb4_0900_ai_ci"

jwt:
  secret: "<long-random-jwt-secret>"

admin:
  users:
    - username: "<admin-username>"
      password: "<admin-password>"

m5stack:
  loginUrl: "https://<m5stack-login-endpoint>"
  registrationUrl: "https://<m5stack-registration-endpoint>"
  queryUsernameUrl: "https://<m5stack-username-query-endpoint>"
  queryMacAddressUrl: "https://<m5stack-device-query-endpoint>"
  registrationToken: "Bearer <registration-token>"
  queryMacAddressToken: "Bearer <device-query-token>"
  issuer: "stackChan"
  audience: "stackChan_user"

xiaozhi:
  secret_key: "<xiaozhi-secret-key>"
  generate_license_token: "<xiaozhi-license-token>"
```

Generate a JWT secret, for example:

```bash
openssl rand -base64 32
```

### 4.4 Run the Service

Development mode:

```bash
go run .
```

Or build first:

```bash
go build -o stackChan .
./stackChan
```

Example local endpoints after startup:

- `http://<your-server-host>:12800/` - admin console
- `http://<your-server-host>:12800/api.json` - OpenAPI JSON
- `http://<your-server-host>:12800/file/...` - static files

### 4.5 Initial Smoke Check

Recommended checks after startup:

1. Open `http://<your-server-host>:12800/` in a browser.
2. Open `http://<your-server-host>:12800/api.json`.
3. Call the admin login API and verify a token is returned.
4. Upload one file and confirm it can be fetched from `/file/...`.

## 5. Configuration Guide

### 5.1 `server`

- `address`: GoFrame listen address, for example `:12800`
- `openapiPath`: OpenAPI JSON output path, currently `/api.json`
- `swaggerPath`: commented out by default; enable only if you intend to expose Swagger UI

Note: the listen address is controlled by `server.address`. `internal/cmd/cmd.go` calls `s.Run()` without overriding the port.

### 5.2 `logger`

Logs are written to `./logs` and also printed to stdout by default.

Useful knobs:

- `level`
- `stdout`
- `rotateExpire`
- `rotateBackup`
- `rotateSize`

### 5.3 `database`

The project currently targets MySQL only.

If you change table definitions, you will usually also want to regenerate GoFrame models:

```bash
make dao
```

Before doing so, make sure `hack/config.yaml` points to the correct database instance.

### 5.4 `jwt`

- Both user JWTs and admin JWTs use `jwt.secret`
- User tokens are generated with a long expiration window
- Admin tokens are generated with a 24-hour expiration

Use a strong secret in any non-test environment.

### 5.5 `admin.users`

This section defines admin console credentials.

Example:

```yaml
admin:
  users:
    - username: "<admin-username>"
      password: "<admin-password>"
```

### 5.6 `m5stack`

This section is used by app-side registration and login.

- `loginUrl`: remote login endpoint
- `registrationUrl`: remote registration endpoint
- `queryUsernameUrl`: base URL for the username availability check; the username is appended to it
- `queryMacAddressUrl`: base URL for the factory-device check; the MAC address is appended to it
- `registrationToken`: remote registration Authorization value
- `queryMacAddressToken`: Authorization value used for the factory-device check
- `issuer` / `audience`: JWT claim values written into locally issued tokens

### 5.7 `rsa`

The device API group and WebSocket entry both rely on an RSA-encrypted MAC token:

- App/device versions encrypt requests with `rsa.server.public`; the server decrypts with `rsa.server.private`
- RSA-encrypted responses use `rsa.client.public`; clients decrypt with `rsa.client.private`

```go
plainText, err := utility.RSADecrypt(cipherText)
encryptedResponse, err := utility.RSAEncrypt(response)
```

Recommended key-generation commands:

```bash
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out server_private.pem
openssl rsa -in server_private.pem -pubout -out server_public.pem
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out client_private.pem
openssl rsa -in client_private.pem -pubout -out client_public.pem
```

### 5.8 `xiaozhi`

Used for XiaoZhi integration.

- `secret_key`: exchanged for the developer token
- `generate_license_token`: returned by the license-token endpoint

The service caches the XiaoZhi token in memory and refresh logic resets it on a 24-hour cycle.

## 6. Admin Console Guide

The built admin console is stored under `web/management` and served directly by the Go server.

Entry URL:

```text
http://<your-server-host>:12800/
```

You can define a shell variable for examples below:

```bash
BASE_URL="http://<your-server-host>:12800"
```

### 6.1 Admin Login

```bash
curl -X POST "$BASE_URL/admin/stackChan/login" \
  -H 'Content-Type: application/json' \
  -d '{
    "user_name": "<admin-username>",
    "pass_word": "<admin-password>"
  }'
```

Success shape:

```json
{
  "code": 0,
  "message": "",
  "data": {
    "token": "<admin-token>"
  }
}
```

Important details:

- All admin APIs except `/admin/stackChan/login` require `Authorization`
- The current middleware expects the raw token value, not `Bearer <token>`

### 6.2 Upload an App Icon, Firmware, or Asset File

```bash
curl -X POST "$BASE_URL/admin/stackChan/uploadFile" \
  -H "Authorization: <admin-token>" \
  -F "file=@./icon.png" \
  -F "name=icon.png" \
  -F "directory=apps"
```

Typical response:

```json
{
  "code": 0,
  "message": "",
  "data": {
    "path": "file/apps/icon.png"
  }
}
```

The public URL is then:

```text
$BASE_URL/file/apps/icon.png
```

### 6.3 Add an App Store Item

```bash
curl -X POST "$BASE_URL/admin/stackChan/app/add" \
  -H "Authorization: <admin-token>" \
  -H 'Content-Type: application/json' \
  -d '{
    "appName": "StackChan Demo",
    "appIconUrl": "'$BASE_URL'/file/apps/icon.png",
    "description": "Demo app for StackChan",
    "firmwareUrl": "'$BASE_URL'/file/apps/demo.bin"
  }'
```

Field summary:

- `appName`: required
- `appIconUrl`: recommended as a full public URL
- `description`: free text description
- `firmwareUrl`: firmware or package download URL

### 6.4 List Apps

```bash
curl -H "Authorization: <admin-token>" \
  "$BASE_URL/admin/stackChan/apps"
```

### 6.5 Update an App

```bash
curl -X PUT "$BASE_URL/admin/stackChan/app/update" \
  -H "Authorization: <admin-token>" \
  -H 'Content-Type: application/json' \
  -d '{
    "id": 1,
    "appName": "StackChan Demo v2",
    "description": "Updated description"
  }'
```

### 6.6 Delete an App

```bash
curl -X DELETE "$BASE_URL/admin/stackChan/app/delete" \
  -H "Authorization: <admin-token>" \
  -H 'Content-Type: application/json' \
  -d '{"id":1}'
```

Deletion is soft delete only: `app_store.is_deleted` is set to `1`.

## 7. App Workflow

App-side APIs are grouped under `/stackChan/v2`.

### 7.1 Register a User

```bash
curl -X POST "$BASE_URL/stackChan/v2/user/registration" \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "<app-username>",
    "email": "<user-email>",
    "password": "<user-password>"
  }'
```

This request is proxied to the configured remote registration endpoint.

### 7.2 Log In

```bash
curl -X POST "$BASE_URL/stackChan/v2/user/login" \
  -H 'Content-Type: application/json' \
  -d '{
    "username": "<app-username>",
    "password": "<user-password>"
  }'
```

The response contains a locally issued JWT, referenced below as `<user-token>`.

### 7.3 Get Current User Info

```bash
curl "$BASE_URL/stackChan/v2/user" \
  -H "token: Bearer <user-token>"
```

### 7.4 Bind a Device

```bash
curl -X POST "$BASE_URL/stackChan/v2/device/bind" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{"mac":"AA:BB:CC:DD:EE:FF"}'
```

On success, the server will:

- Create the device record if it does not exist
- Store the current user UID into `device.uid`
- Set `bind_time`

### 7.5 List Devices for the Current User

```bash
curl "$BASE_URL/stackChan/v2/devices" \
  -H "token: Bearer <user-token>"
```

### 7.6 Update a Device

```bash
curl -X PUT "$BASE_URL/stackChan/v2/device/update" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "name": "Desk StackChan"
  }'
```

Note: the request struct includes `longitude` and `latitude`, but the default SQL schema does not currently create those columns in the `device` table. Add the columns first if you plan to use them.

### 7.7 Unbind a Device

```bash
curl -X POST "$BASE_URL/stackChan/v2/device/unbind" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{"mac":"AA:BB:CC:DD:EE:FF"}'
```

This flow attempts to:

- Restore the default XiaoZhi agent configuration
- Unbind the device on the XiaoZhi side
- Clear the local bind relationship

### 7.8 Restore the Default Agent Configuration

```bash
curl -X POST "$BASE_URL/stackChan/v2/device/agent/restore" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{"mac":"AA:BB:CC:DD:EE:FF"}'
```

### 7.9 Manage Dances

List dances for a specific device:

```bash
curl -G "$BASE_URL/stackChan/v2/dance" \
  -H "token: Bearer <user-token>" \
  --data-urlencode "mac=AA:BB:CC:DD:EE:FF"
```

Create a dance:

```bash
curl -X POST "$BASE_URL/stackChan/v2/dance" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{
    "mac": "AA:BB:CC:DD:EE:FF",
    "danceName": "wave-demo",
    "musicUrl": "'$BASE_URL'/file/music/stackchan_music.mp3",
    "danceData": [
      {
        "leftEye": {"x":0,"y":0,"rotation":0,"weight":100,"size":0},
        "rightEye": {"x":0,"y":0,"rotation":0,"weight":100,"size":0},
        "mouth": {"x":0,"y":0,"rotation":0,"weight":0,"size":0},
        "pitchServo": {"angle":450,"speed":900},
        "yawServo": {"angle":0,"speed":900},
        "leftRgbColor": "#AAAAAA",
        "rightRgbColor": "#AAAAAA",
        "durationMs": 1000
      }
    ]
  }'
```

Get dance details:

```bash
curl -G "$BASE_URL/stackChan/v2/danceData" \
  -H "token: Bearer <user-token>" \
  --data-urlencode "id=1"
```

Update a dance:

```bash
curl -X PUT "$BASE_URL/stackChan/v2/dance" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{"id":1,"danceName":"wave-demo-v2"}'
```

Delete a dance:

```bash
curl -X DELETE "$BASE_URL/stackChan/v2/dance" \
  -H "token: Bearer <user-token>" \
  -H 'Content-Type: application/json' \
  -d '{"id":1}'
```

## 8. Device Workflow

Device-side APIs mainly use `/stackChan` plus `/stackChan/ws`.

### 8.1 Build the Device Authorization Header

Both the device REST APIs and WebSocket endpoint expect an RSA-encrypted MAC token.

Plain text format:

```text
<mac>|<nonce>|<unix_timestamp_seconds>
```

Generation logic:

```text
plainText = "AA:BB:CC:DD:EE:FF|random_nonce|1710000000"
cipher    = RSA-OAEP-SHA256-Encrypt(plainText, rsa.server.public)
header    = Base64(cipher)
```

Request header:

```http
Authorization: <rsa-mac-token>
```

The server will:

- Base64-decode the header
- Decrypt it using `rsa.server.private`
- Extract the MAC from the payload
- Reject tokens whose timestamp differs from server time by more than 10 seconds

### 8.2 Create a Device Record

```bash
curl -X POST "$BASE_URL/stackChan/device" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{"name":"My StackChan"}'
```

### 8.3 Get Device Info

```bash
curl "$BASE_URL/stackChan/device/info" \
  -H "Authorization: <rsa-mac-token>"
```

### 8.4 Update Device Info

```bash
curl -X PUT "$BASE_URL/stackChan/device/info" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{"name":"Living Room StackChan"}'
```

### 8.5 Get the User Bound to This Device

```bash
curl "$BASE_URL/stackChan/device/user" \
  -H "Authorization: <rsa-mac-token>"
```

### 8.6 Unbind from the Device Side

```bash
curl -X POST "$BASE_URL/stackChan/device/unbind" \
  -H "Authorization: <rsa-mac-token>"
```

### 8.7 Fetch Random Online Devices

```bash
curl -G "$BASE_URL/stackChan/device/randomList" \
  -H "Authorization: <rsa-mac-token>" \
  --data-urlencode "pageSize=6"
```

This list is drawn from the currently connected StackChan WebSocket clients and excludes the caller's own MAC.

### 8.8 Friend, Post, Comment, and Pano APIs

Add a friend device:

```bash
curl -X POST "$BASE_URL/stackChan/friend" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{"friendMac":"11:22:33:44:55:66"}'
```

Create a post:

```bash
curl -X POST "$BASE_URL/stackChan/post/add" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{
    "content_text": "hello stackchan",
    "content_image": "'$BASE_URL'/file/posts/a.jpg"
  }'
```

List posts:

```bash
curl -G "$BASE_URL/stackChan/post/get" \
  -H "Authorization: <rsa-mac-token>" \
  --data-urlencode "page=1" \
  --data-urlencode "pageSize=10"
```

Create a comment:

```bash
curl -X POST "$BASE_URL/stackChan/post/comment/create" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{"postId":1,"content":"nice!"}'
```

List comments:

```bash
curl -G "$BASE_URL/stackChan/post/comment/get" \
  -H "Authorization: <rsa-mac-token>" \
  --data-urlencode "postId=1" \
  --data-urlencode "page=1" \
  --data-urlencode "pageSize=10"
```

Add a panoramic image record:

```bash
curl -X POST "$BASE_URL/stackChan/pano" \
  -H "Authorization: <rsa-mac-token>" \
  -H 'Content-Type: application/json' \
  -d '{"url":"'$BASE_URL'/file/pano/p1.jpg"}'
```

List panoramic images:

```bash
curl "$BASE_URL/stackChan/pano" \
  -H "Authorization: <rsa-mac-token>"
```

### 8.9 Upload a File from the Device Side

```bash
curl -X POST "$BASE_URL/stackChan/uploadFile" \
  -H "Authorization: <rsa-mac-token>" \
  -F "file=@./photo.jpg" \
  -F "name=photo.jpg" \
  -F "directory=posts"
```

### 8.10 Public App List and XiaoZhi APIs

Get the public app list:

```bash
curl "$BASE_URL/stackChan/apps"
```

Get the current XiaoZhi developer token:

```bash
curl "$BASE_URL/stackChan/xiaozhi/token"
```

Force a token refresh:

```bash
curl "$BASE_URL/stackChan/xiaozhi/token/refresh"
```

Get the license token:

```bash
curl "$BASE_URL/stackChan/xiaozhi/generateLicenseToken"
```

## 9. Authentication and Response Format

### 9.1 Authentication Rules

| Group | Prefix | Header | Notes |
| --- | --- | --- | --- |
| Admin | `/admin/stackChan` | `Authorization: <admin-token>` | `/login` is public |
| App | `/stackChan/v2` | `token: Bearer <user-token>` | `/user/login` and `/user/registration` are public |
| Device | `/stackChan` | `Authorization: <rsa-mac-token>` | Most APIs depend on MAC extraction |
| WebSocket | `/stackChan/ws` | `Authorization: <rsa-mac-token>` | Query parameter `deviceType` is also required |
| Static files | `/file/*` | none | Direct file access |

Additional notes:

- The app middleware strips the `Bearer ` prefix automatically
- The admin middleware expects the raw token value
- Some `/stackChan` routes do not hard-fail if no MAC is present, but clients should still follow the standard header format consistently

### 9.2 Unified Response Shape

GoFrame wraps normal API responses in this shape:

```json
{
  "code": 0,
  "message": "",
  "data": {}
}
```

On failure:

- `code` contains an error code
- `message` contains the error description
- `data` is usually `null`

## 10. API Reference by Module

### 10.1 Admin APIs: `/admin/stackChan`

| Method | Path | Purpose | Main Parameters |
| --- | --- | --- | --- |
| POST | `/login` | Admin login | `user_name`, `pass_word` |
| POST | `/uploadFile` | Upload icon, firmware, or asset file | form-data: `file`, `name`, `directory` |
| POST | `/app/add` | Create App Store item | `appName`, `appIconUrl`, `description`, `firmwareUrl` |
| GET | `/apps` | List apps | none |
| PUT | `/app/update` | Update app | `id` required, others optional |
| DELETE | `/app/delete` | Soft delete app | `id` |

### 10.2 App APIs: `/stackChan/v2`

| Method | Path | Purpose | Main Parameters |
| --- | --- | --- | --- |
| POST | `/user/registration` | Register user | `username`, `email`, `password` |
| POST | `/user/login` | Log in and obtain JWT | `username`, `password` |
| GET | `/user` | Get current user | header `token` |
| GET | `/devices` | List current user's devices | header `token` |
| POST | `/device/bind` | Bind device | `mac` |
| POST | `/device/unbind` | Unbind device | `mac` |
| PUT | `/device/update` | Update bound device | `mac`, optional `name`, `longitude`, `latitude` |
| POST | `/device/agent/restore` | Restore default agent config | `mac` |
| GET | `/dance` | List dances for a device | query `mac` |
| POST | `/dance` | Create dance for a device | `mac`, `danceName`, `danceData`, `musicUrl` |
| GET | `/danceData` | Get dance details | query `id` |
| PUT | `/dance` | Update dance | `id` plus optional fields |
| DELETE | `/dance` | Delete dance | `id` |

### 10.3 Device APIs: `/stackChan`

| Method | Path | Purpose | Main Parameters |
| --- | --- | --- | --- |
| POST | `/device` | Create device record | `name` |
| PUT | `/device` | Update device name | `name` |
| GET | `/device/info` | Get current device info | none |
| PUT | `/device/info` | Update current device info | `name` |
| GET | `/device/randomList` | List random online devices | query `pageSize` |
| GET | `/device/user` | Get bound user for current device | none |
| POST | `/device/unbind` | Unbind from device side | none |
| POST | `/friend` | Add friend device | `friendMac` |
| GET | `/dance` | List current device dances | none |
| POST | `/dance` | Create dance for current device | `danceName`, `danceData`, `musicUrl` |
| GET | `/danceData` | Get dance details | query `id` |
| PUT | `/dance` | Update dance | `id` plus optional fields |
| DELETE | `/dance` | Delete dance | `id` |
| GET | `/musicList` | List built-in music files | none |
| POST | `/pano` | Add pano record | `url` |
| GET | `/pano` | List pano records | none |
| POST | `/uploadFile` | Upload file | form-data: `file`, `name`, `directory` |
| GET | `/apps` | Get public App Store list | none |
| GET | `/xiaozhi/token` | Get XiaoZhi token | none |
| GET | `/xiaozhi/token/refresh` | Force XiaoZhi token refresh | none |
| GET | `/xiaozhi/generateLicenseToken` | Get license token | none |

### 10.4 Community APIs: `/stackChan`

| Method | Path | Purpose | Main Parameters |
| --- | --- | --- | --- |
| POST | `/post/add` | Create post | `content_text`, optional `content_image` |
| GET | `/post/get` | Paginated post list | `page`, `pageSize` |
| DELETE | `/post/delete` | Delete post | `id` |
| POST | `/post/comment/create` | Create comment | `postId`, `content` |
| GET | `/post/comment/get` | Paginated comment list | `postId`, `page`, `pageSize` |
| DELETE | `/post/comment/delete` | Delete comment | `postId`, `commentId` |

### 10.5 Public App Store Response

Public list endpoint:

```bash
curl "$BASE_URL/stackChan/apps"
```

Returned fields usually include:

- `id`
- `appName`
- `appIconUrl`
- `description`
- `firmwareUrl`
- `createAt`
- `updateAt`

## 11. WebSocket Protocol

Example endpoint URLs:

```text
ws://<your-server-host>:12800/stackChan/ws?deviceType=StackChan
ws://<your-server-host>:12800/stackChan/ws?deviceType=App&deviceId=<app-device-id>
```

Requirements:

- `Authorization: <rsa-mac-token>` must be sent
- `deviceType=StackChan` means a hardware client
- `deviceType=App` means an app client and also requires `deviceId`

### 11.1 Binary Frame Format

```text
1 byte  : msgType
4 bytes : payload length (big endian)
N bytes : payload
```

### 11.2 Message Types

| Value | Name | Meaning |
| --- | --- | --- |
| `0x01` | `Opus` | Audio frame |
| `0x02` | `Jpeg` | Image frame |
| `0x03` | `ControlAvatar` | Avatar / expression control |
| `0x04` | `ControlMotion` | Motion control |
| `0x05` | `OnCamera` | Turn camera on |
| `0x06` | `OffCamera` | Turn camera off |
| `0x07` | `TextMessage` | Text message |
| `0x09` | `RequestCall` | Request call |
| `0x0A` | `RefuseCall` | Refuse call |
| `0x0B` | `AgreeCall` | Accept call |
| `0x0C` | `HangupCall` | Hang up |
| `0x0D` | `UpdateDeviceName` | Update device name |
| `0x0E` | `GetDeviceName` | Query device name |
| `0x10` | `ping` | Server heartbeat |
| `0x11` | `pong` | Client heartbeat response |
| `0x12` | `OnPhoneScreen` | Start phone-screen projection |
| `0x13` | `OffPhoneScreen` | Stop phone-screen projection |
| `0x14` | `Dance` | Dance playback message |
| `0x15` | `GetAvatarPosture` | Query avatar posture |
| `0x16` | `DeviceOffline` | Device offline notification |
| `0x17` | `DeviceOnline` | Device online notification |
| `0x18` | `OnAudio` | Start audio subscription |
| `0x19` | `OffAudio` | Stop audio subscription |
| `0x1A` | `AimedTakePhoto` | Directed photo capture |

### 11.3 Timers

At startup, the server launches two recurring tasks:

- every 5 seconds: send WebSocket heartbeat messages
- every 15 seconds: clean expired connections

## 12. Database Overview

Schema file:

- `check_list/create_mysql_database.sql`

Main tables:

| Table | Purpose |
| --- | --- |
| `user` | Cached local user profile, keyed by remote UID |
| `device` | Device table, keyed by `mac` |
| `device_dance` | Dance motion data |
| `device_friend` | Device friendship links |
| `device_pano` | Panoramic image records |
| `device_post` | Device-created posts |
| `device_post_comment` | Post comments |
| `app_store` | Admin-managed app list |

Additional notes:

- `app_store` uses soft delete through `is_deleted`
- `device.bind_time` is currently stored as a string, not a datetime type
- The SQL script does not create the `stackChan` database itself; create it first

## 13. Project Structure

```text
.
├── api/                         # GoFrame API definitions
│   ├── admin/                   # Admin APIs
│   ├── appstore/                # Public App Store APIs
│   ├── dance/                   # Dance APIs
│   ├── device/                  # Device and app-device APIs
│   ├── file/                    # Upload APIs
│   ├── friend/                  # Friend APIs
│   ├── pano/                    # Pano APIs
│   ├── post/                    # Post and comment APIs
│   ├── stackchandevice/         # Device-bound-user and device-unbind APIs
│   ├── user/                    # User login and registration APIs
│   └── xiaozhi/                 # XiaoZhi APIs
├── check_list/                  # MySQL bootstrap SQL
├── file/                        # Uploaded files and built-in music
├── hack/                        # GoFrame CLI and generation config
├── internal/
│   ├── boot/                    # Heartbeat and cleanup schedulers
│   ├── cmd/                     # HTTP server startup and route binding
│   ├── controller/              # Controllers
│   ├── dao/                     # GoFrame DAO layer
│   ├── middleware/              # Auth and CORS middleware
│   ├── model/                   # Business models and entities
│   ├── packed/                  # Embedded/static resource linkage
│   ├── service/                 # Service layer
│   ├── web_socket/              # WebSocket pools and forwarding
│   └── xiaozhi/                 # XiaoZhi client logic
├── manifest/
│   ├── config/                  # Runtime config
│   ├── deploy/                  # Kustomize templates
│   └── docker/                  # Docker build files
├── resource/                    # Template/resource directory
├── utility/                     # Helpers such as RSA decryption
├── web/management/              # Built Flutter Web admin assets
├── LICENSE
├── Makefile
├── README.MD
└── main.go
```

## 14. Development, Build, and Deployment

### 14.1 Common Commands

```bash
# Run directly
go run .

# Build a binary
go build -o stackChan .

# Build via GoFrame CLI
make build

# Generate controller/sdk code
make ctrl

# Generate DAO/DO/Entity from the database
make dao

# Build Docker image
make image
```

Notes:

- `make build`, `make dao`, and `make ctrl` rely on the `gf` CLI
- If `gf` is not installed, the Makefile attempts to download and install it automatically

### 14.2 Docker

Dockerfile path:

- `manifest/docker/Dockerfile`

Expected inputs:

- compiled binary named `stackChan`
- a `config.yaml` file placed in the build context

Typical flow:

```bash
go build -o stackChan .
cp manifest/config/config.yaml ./config.yaml
docker build -f manifest/docker/Dockerfile -t stackchan-server:local .
```

### 14.3 Kubernetes

The repository includes `manifest/deploy/kustomize`, but treat it as a starter template rather than production-ready deployment.

Before using it, review at least:

- service names still based on `template-single`
- target port still based on `8000`
- placeholder deployment metadata
- environment-specific config alignment with your real service port and image name

## 15. Operational Notes

1. The admin console is shipped as static assets
   - The repository contains the built output under `web/management`
   - It does not include the Flutter source project for that frontend

2. There is a hard-coded external music URL in the v2 dance default path
   - Review `internal/controller/dance/dance_v2_get_list.go`
   - Replace it with your own public file URL or local deployment URL before production use

3. The device update API exposes `longitude` and `latitude`
   - The default schema does not create those columns
   - Add the columns first if you want to enable that feature cleanly

4. Do not publish environment-specific config values as-is
   - Replace admin credentials, JWT secret, registration token, RSA keys, and XiaoZhi secrets before deployment
   - If you plan to open-source the full repository, sanitize runtime config files separately from this README

5. Admin and app token headers are intentionally different
   - Admin: `Authorization: <admin-token>`
   - App: `token: Bearer <user-token>`

## 16. License

This project is released under the MIT License.

See the root file:

- `LICENSE`

```text
MIT License
Copyright (c) 2026 M5Stack Technology CO LTD
```
