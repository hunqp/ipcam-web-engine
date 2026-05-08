# FastCGI Server

This project provides a FastCGI-based server environment for communication between a web browser and backend device services.

## Overview

FastCGI is a protocol used to connect interactive programs with a web server.  
In this project, FastCGI acts as a gateway between browser requests and backend device applications.

The server uses Lighttpd together with FastCGI services.

---

## Prerequisites

Before building or running the project, load the environment setup script:

```bash
source ./envsetup.sh
```

---

## Build

Build the project using:

```bash
make
make flash # Install FastCGI
```

Additional build targets may be available in the `Makefile`.

---

## Run

Start the runtime environment:

```bash
cd envir
./env-run.sh
```

This launches the Lighttpd server and FastCGI services.

---

## Test

Open your browser and access:

```text
http://localhost:8080
```

---

## Project Structure

```text
.
├── 3rd-party                  # External dependencies
├── cgi-bin
│   └── sources
│       ├── 3rd-party
│       │   ├── jwt            # JWT library
│       │   ├── libfcgi        # FastCGI library
│       │   └── libopenssl     # OpenSSL library
│       └── impl               # FastCGI implementation source
└── envir
    ├── bin                    # Runtime executables
    ├── etc                    # Configuration files
    ├── lib                    # Shared libraries
    └── www
        └── records            # Web content / generated records
```

---

## Runtime Environment

The runtime environment is located under the `envir/` directory:

- `bin/` contains executable binaries
- `etc/` contains Lighttpd and application configuration files
- `lib/` contains required shared libraries
- `www/` contains web-accessible resources

---

## Dependencies

This project includes the following third-party libraries:

- FastCGI (`libfcgi`)
- OpenSSL (`libopenssl`)
- JWT library (`jwt`)

---

## Notes

- Ensure `ENVIR_DIR` is configured correctly before running the server.
- Linux/Unix environments are recommended.
- Lighttpd must support FastCGI modules.

---

## Author

Pham Nguyen Quoc Hung