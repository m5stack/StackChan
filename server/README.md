# StackChan Server

**StackChan Server** is the Server of the open-source StackChan project. It handles core functionalities such
as device interactions, post management, and comment systems, providing stable and efficient API support.

---

## Features

- App and StackChan communication and interaction
- Device post creation and management (text and image support, similar to a social feed)
- Comment CRUD (Create, Read, Update, Delete) operations
- Dance control and data management
- Persistent storage using a relational database

---

## Getting Started

### Prerequisites

- **Go**: The project is developed in Go. Install **Go 1.24+** from
  the [official download page](https://golang.google.cn/dl/).

Verify installation:

```bash
go version
# Expected output: "go version go1.24.x ..." (or similar)

### Clone the Repository
```bash
git clone https://github.com/m5stack/StackChan  # Replace with the actual repository URL
cd StackChan/server

# Download dependencies
go mod download

# build
go build -o StackChan main.go

# Start running
StackChan    # Linux/macOS
StackChan.exe  # Windows
