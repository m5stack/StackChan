# StackChan Server

**StackChan Server** is the backend server for the open-source StackChan project. It provides RESTful APIs for device management, user authentication, post management, comment systems, and dance control functionalities.

---

## Features

- **Device Management**: Device binding, unbinding, and information updates
- **User Authentication**: Login, registration, and JWT-based token authentication
- **Post System**: Create, read, and manage posts with text and image support
- **Comment System**: Full CRUD operations for post comments
- **Dance Control**: Dance creation, management, and motion data handling
- **App Store Integration**: App management and distribution
- **AI Agent Integration**: XiaoZhi (小智) AI assistant integration with agent configuration
- **Persistent Storage**: MySQL database with ORM-based data access

---

## Table of Contents

- [Prerequisites](#prerequisites)
- [Database Setup](#database-setup)
- [Configuration](#configuration)
- [Installation](#installation)
- [Running the Server](#running-the-server)
- [API Documentation](#api-documentation)
- [Project Structure](#project-structure)
- [Contributing](#contributing)
- [License](#license)

---

## Prerequisites

- **Go**: The project is developed in Go. Install **Go 1.24+** from
  the [official download page](https://golang.google.cn/dl/).

Verify installation:

```bash
go version
# Expected output: "go version go1.24.x ..." (or similar)
```

- **MySQL**: Version 8.0+ for database storage
- **Git**: For cloning the repository

---

## Database Setup

Before running the project, you need to create the MySQL database first:

```bash
# Execute the database initialization script
mysql -u your_username -p < check_list/create_mysql_database.sql
```

This will create the `stackChan` database and all required tables.

---

## Configuration

Update the `manifest/config/config.yaml` file with your actual configuration:

### 1. Database Connection

```yaml
database:
  default:
    link: "mysql:your_username:your_password@tcp(127.0.0.1:3306)/stackChan?charset=utf8mb4&collation=utf8mb4_0900_ai_ci"
```

### 2. JWT Configuration

For production use, generate your own secure JWT secret key.

**Generate a secure JWT secret key:**

Option 1: Using OpenSSL (recommended)
```bash
# Generate a 32-byte (256-bit) random secret key encoded in base64
openssl rand -base64 32
```

Option 2: Using Python
```bash
python3 -c "import secrets; import base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

Update the configuration in `manifest/config/config.yaml`:
```yaml
jwt:
  secret: "your_generated_secret_key_here"
```

### 3. RSA Keys

For production use, generate your own RSA key pairs for encryption.

### 4. XiaoZhi Configuration

Set your XiaoZhi (小智) API secret key:

```yaml
xiaozhi:
  secret_key: "your_xiaozhi_secret_key_here"
  generate_license_token: "your_xiaozhi_generate_license_token_here"
```

---

## Installation

```bash
# Clone the repository
git clone https://github.com/m5stack/StackChan.git
cd StackChan/server

# Download dependencies
go mod download
```

---

## Running the Server

### Development Mode

```bash
go run main.go
```

### Build and Run

```bash
# Build for current platform
go build -o stackchan-server main.go

# Run
./stackchan-server    # Linux/macOS
stackchan-server.exe  # Windows
```

### Using Makefile

```bash
# Build
make build

# Run
make run
```

---

## API Documentation

The server provides RESTful APIs organized by module:

### Device APIs (`/api/device/*`)
- `POST /device/bind` - Bind a device to the current user
- `POST /device/unbind` - Unbind a device from the current user
- `PUT /device/update` - Update device information
- `GET /devices` - Get list of devices

### User APIs (`/api/user/*`)
- `POST /user/login` - User login
- `POST /user/registration` - User registration
- `GET /user` - Get user information

### Dance APIs (`/api/dance/*`)
- `POST /dance` - Create a new dance
- `GET /dance` - Get dance information
- `PUT /dance` - Update dance details
- `DELETE /dance` - Delete a dance

### Post APIs (`/api/post/*`)
- `GET /post/get` - Get post details
- Post listing and comment operations

### Admin APIs (`/api/admin/*`)
- App management operations
- User management

---

## Project Structure

```
stackChan/
├── api/                    # API definitions and request/response structures
│   ├── admin/             # Admin module APIs
│   ├── appstore/          # App store module APIs
│   ├── dance/             # Dance module APIs
│   ├── device/            # Device module APIs
│   ├── friend/            # Friend module APIs
│   ├── post/              # Post module APIs
│   ├── user/              # User module APIs
│   └── xiaozhi/           # XiaoZhi AI module APIs
├── internal/              # Internal application code
│   ├── boot/             # Boot initialization
│   ├── cmd/              # Command entry
│   ├── consts/           # Constants definitions
│   ├── controller/       # API controllers
│   ├── dao/              # Data Access Objects
│   ├── logic/            # Business logic
│   ├── middleware/       # HTTP middlewares
│   ├── model/            # Data models
│   ├── packed/           # Packed assets
│   ├── service/          # Business services
│   └── xiaozhi/          # XiaoZhi integration
├── manifest/             # Deployment and configuration
│   ├── config/           # Configuration files
│   ├── deploy/           # Deployment scripts
│   └── docker/           # Docker configurations
├── utility/              # Utility functions (RSA, etc.)
├── web/                  # Web frontend assets
├── main.go               # Application entry
├── go.mod                # Go module definition
├── go.sum                # Go dependencies checksum
├── Makefile              # Build scripts
└── README.MD             # This file
```

---

## Architecture Overview

The project follows a layered architecture:

1. **API Layer**: Defines request/response structures and routes
2. **Controller Layer**: Handles HTTP requests and responses
3. **Service Layer**: Implements business logic
4. **DAO Layer**: Data access and database operations
5. **Model Layer**: Data structures and entities

The project uses the GoFrame framework for rapid development and robust infrastructure.

---

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

For major changes, please open an issue first to discuss what you would like to change.

### Development Guidelines

- Follow Go conventions and best practices
- Write clear, descriptive comments in English
- Add tests for new features
- Ensure all existing tests pass
- Update documentation as needed

---

## License

[SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD](LICENSE)

[SPDX-License-Identifier: MIT](LICENSE)

---

## Support

For questions and support, please open an issue in the GitHub repository or contact the M5Stack team.

---

## Acknowledgments

- [GoFrame](https://goframe.org/) - The Go framework used in this project
- [M5Stack](https://m5stack.com/) - For supporting open-source development
- All contributors who help improve this project