# 📡 TCP/UDP Message Broker

A high-performance **Publish/Subscribe message broker** implemented in **C**. This system enables efficient communication between **UDP publishers** and **TCP subscribers**, routing messages based on topic subscriptions with low latency and high reliability.

---

## 🚀 Overview

This project implements a messaging platform with a central broker that connects two different communication models:

* **UDP Clients (Publishers)** → Send messages tagged with topics.
* **TCP Clients (Subscribers)** → Subscribe to topics and receive matching messages in real-time.

The broker ensures that messages are delivered only to the relevant subscribers.

---

## 🏗️ Architecture

### 🔹 Broker (Server)

* Acts as the **central hub** of the system.
* Receives messages from UDP publishers.
* Matches messages against subscriber topics.
* Forwards messages to the appropriate TCP clients.

### 🔹 Subscribers (TCP Clients)

* Connect to the broker via TCP.
* Subscribe or unsubscribe from topics.
* Receive and display messages in real-time.

---

## 🧠 Key Features

### ⚡ I/O Multiplexing

* Uses `select()` to handle:

  * TCP listening socket
  * UDP socket
  * Standard input (`stdin`)
  * Active TCP client connections
* Ensures **non-blocking concurrent communication**.

---

### 📦 Custom TCP Framing

* Solves TCP stream issues (fragmentation & concatenation).
* Uses a **4-byte header (network byte order)**:

  * Specifies payload length (`net_len`)
* Guarantees **correct message reconstruction**.

---

### 🧩 Topic Wildcard Matching

Supports flexible subscription patterns:

* `+` → Matches a **single level**
* `*` → Matches **multiple levels**

✔ Implemented using a **recursive segment-matching algorithm**
✔ Enables powerful and dynamic topic filtering

---

### 🔄 Persistent Sessions

* Clients are identified by a **unique Client ID**.
* Subscription state is preserved even after disconnect.
* On reconnect:

  * Previous subscriptions are automatically restored.

---

### ⚡ Low-Latency Optimizations

* Disables **Nagle’s Algorithm** (`TCP_NODELAY`)
* Disables stdout buffering (`_IONBF`)
* Ensures **instant message delivery and display**

---

## 📁 Project Structure

```
.
├── server.c        # Core broker implementation
├── subscriber.c    # TCP client for subscriptions
├── test.py         # Automated testing suite
├── Makefile        # Build configuration
└── README.md       # Project documentation
```

---

## 💻 Build & Run

### 🔧 Compilation

```bash
make all
```

---

### ▶️ Run the Server (Broker)

```bash
./server <PORT>
```

---

### 👤 Run a Subscriber

```bash
./subscriber <CLIENT_ID> <SERVER_IP> <SERVER_PORT>
```

---

### 🧹 Cleanup

```bash
make clean
```

---

## 🧪 Testing

The `test.py` script provides automated testing for:

* Client connections and reconnections
* Topic subscriptions/unsubscriptions
* Wildcard matching behavior
* Message delivery correctness

Run tests with:

```bash
python3 test.py
```

---

## 🎯 Design Goals

* High performance and low latency
* Reliable TCP message handling
* Flexible topic-based routing
* Scalable client management
* Clean and modular C implementation

---

## 📌 Future Improvements

* Message persistence (disk storage)
* QoS levels (like MQTT)
* Authentication & security
* Multithreading support (beyond `select()` limits)

---

## 📜 License

This project is open-source and available under the MIT License.

---

## 👨‍💻 Author

Developed as a systems programming project focused on **networking, concurrency, and protocol design in C**.

---

⭐ If you found this project useful, consider giving it a star!
