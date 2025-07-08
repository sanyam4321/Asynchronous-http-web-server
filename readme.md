# Asynchronous Event Loop

## Overview

---

This is a **Linux-only** library for high-performance, asynchronous I/O using an event loop and callback-based design. It demonstrates how to build scalable client-server architectures using non-blocking networking and database interactions.

---

## Features

* Epoll-based I/O multiplexing
* Callback-based asynchronous flow
* Asynchronous PostgreSQL queries using `libpq`
* Connection pooling for efficient DB access
* Fully asynchronous end-to-end service: Client → Third-party API → DB
* Port reuse support allows running multiple server instances concurrently
* Modular structure with reusable components

---

## Architecture Overview

The server is capable of handling any combination of I/O tasks:

| Request Path                  | Description                                                               |
| ----------------------------- | ------------------------------------------------------------------------- |
| Client → DB                   | Pure database-driven route handled asynchronously via libpq and callbacks |
| Client → Third-party API      | External HTTP call handled using non-blocking sockets and event loop      |
| Client → Third-party API → DB | Fully asynchronous chain involving both external APIs and DB writes/reads |
| Client → DB → Third-party API | Asynchronous DB interaction followed by external API call                 |

All execution paths are **non-blocking**, ensuring the server can continue handling other clients while awaiting DB or network responses.

---

## Prerequisites

Ensure you're on a **Debian-based Linux** distribution like Ubuntu.

### 1. Install Required Packages

```bash
sudo apt update
sudo apt install -y build-essential cmake libpq-dev clang
```

### 2. Verify PostgreSQL Headers

Ensure `libpq-fe.h` is installed (used by the server for async DB interaction):

```bash
ls /usr/include/postgresql/libpq-fe.h
```

If missing, re-run the install:

```bash
sudo apt install libpq-dev
```

---

## Project Structure

```
Asynchronous-http-web-server/
├── include/               # Header files
├── lib/                   # Contains libllhttp.a
├── example/               # Example files (client.cpp, server.cpp)
├── client                 # Built client binary
├── server                 # Built server binary
├── Makefile               # Build system
└── readme.md              # This file
```

---

## Build Instructions

### 1. Clone the Repository

```bash
git clone https://github.com/sanyam4321/Asynchronous-http-web-server
cd Asynchronous-http-web-server
```

### 2. Build Examples

```bash
make
```

This compiles:

* `client` → A lightweight test client
* `server` → The asynchronous server that interacts with PostgreSQL and external APIs

To clean up binaries:

```bash
make clean
```

---

## Running the Programs

You may need to adjust the PostgreSQL connection string in `server.cpp` before running.

### Run the Server

```bash
./server <listen_address> <listen_port>
```

The server supports **SO\_REUSEPORT**, allowing multiple instances to bind the same IP\:PORT pair for scalability.

### Run the Client

```bash
./client <server_address> <server_port>
```

### Run the Test Script (your own test script)

If included:

```bash
./run_clients.sh
```

---

## Notes

* Designed for Linux. Will not compile on Windows/macOS.
* Requires PostgreSQL running locally or remotely with accessible credentials.
* Ensure `libllhttp.a` is prebuilt in `lib/`. You can build it from the llhttp source if needed.
* Callback design allows handling 10,000+ concurrent connections efficiently.

---

## License

MIT
