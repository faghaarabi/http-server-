# 🖥️ HTTP Server with Dynamic Content & Database (Assignment 3)

## 📌 Overview
This project implements a simple HTTP/1.0 server in C that supports:

- Serving static files (HTML, images, CSS, JS)
- Handling GET, HEAD, and POST requests
- Storing POST data using a GDBM (ndbm-style) database
- Basic security checks (path validation)
- Concurrent client handling

---

## ⚙️ Features

### 🌐 HTTP Support
- **GET** → retrieve files
- **HEAD** → retrieve headers only
- **POST** → send data to server

---

### 📁 Static File Serving
- Serves files from the `www/` directory
- Supports:
    - `text/html`
    - `image/jpeg`, `image/png`, `image/gif`
    - `text/css`
    - `application/javascript`
    - `text/plain`

---

### 💾 Data Persistence
- Stores POST data in a **GDBM database**
- Uses **unique keys** (timestamp + PID)

---

### 🔒 Security
- Prevents directory traversal attacks (`..`)
- Validates HTTP requests
- Handles invalid input safely

---

### ⚡ Concurrency
- Supports multiple clients simultaneously
- Tested using parallel `curl` requests

---

## 🗂️ Project Structure  
.
├── src/
│ └── server.c
├── lib/
│ └── http_handler.c
├── include/
├── www/
│ └── index.html
├── db/
│ └── data.db
├── db_reader.c
├── Makefile
└── README.md


---

## 🔧 Build Instructions and Run

```bash
clang -Wall -Wextra -std=c11 -Iinclude src/server.c -o server_app -ldl
clang -Wall -Wextra -std=c11 -Iinclude -fPIC -shared lib/http_handler.c -o lib/libhttp.so -lgdbm_compat -lgdbm
clang -Wall -Wextra -std=c11 db_reader.c -o db_reader -lgdbm_compat -lgdbm


./server_app

http://localhost:9090