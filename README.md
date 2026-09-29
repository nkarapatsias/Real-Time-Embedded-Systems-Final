# Real-Time Embedded Systems — Final Project 2026

![Platform](https://img.shields.io/badge/Platform-Raspberry%20Pi-C51A4A?logo=raspberry-pi&logoColor=white)
![Language](https://img.shields.io/badge/Language-C99-00599C?logo=c&logoColor=white)
![Threading](https://img.shields.io/badge/Concurrency-POSIX%20Threads-blue)
![Architecture](https://img.shields.io/badge/Pattern-Producer--Consumer-success)

## 📌 Project Overview

This project implements a multi-threaded, real-time embedded telemetry ingestion and processing engine on Linux targeting the **Raspberry Pi 3B** (or equivalent single-board computer).

The system subscribes asynchronously to the global **Bluesky Jetstream Firehose**, buffers and parses incoming JSON events, maintains real-time statistics under strict mutual exclusion, and samples deterministic system metrics with sub-millisecond precision.

---

## 🎯 Target Stream Specification

The system connects to the public Bluesky Jetstream WebSocket feed over the AT Protocol:

* **Endpoint:** `wss://jetstream1.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post`
* **Data Format:** Text-based JSON objects.
* **Filter Target:** Inspect and classify each message based on its `"kind"` field.

---

## 🏗️ System Architecture

The application is written strictly in **User-Space C** using a **Bounded Circular Buffer Producer-Consumer** design pattern synchronized via POSIX threads (`pthreads`), mutexes (`pthread_mutex_t`), and condition variables (`pthread_cond_t`).

```text
                              ┌─────────────────────────────────────────┐
                              │      AT Protocol Bluesky Firehose       │
                              └────────────────────┬────────────────────┘
                                                   │ WebSocket (Frames)
                                                   ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ [Thread 1: Producer]                                                                   │
│ Asynchronous event loop (`libwebsockets`) fetches raw frames and enqueues payload.     │
└──────────────────────────────────────────────────┬─────────────────────────────────────┘
                                                   │ Enqueue + pthread_cond_signal
                                                   ▼
                                    ┌─────────────────────────────┐
                                    │ Bounded Circular Queue      │
                                    │ (Thread-Safe Ring Buffer)   │
                                    └──────────────┬──────────────┘
                                                   │ Dequeue
                                                   ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ [Thread 2: Consumer]                                                                   │
│ Event-driven worker parses JSON payload (`cJSON` / `jsmn`) and updates counters.       │
└──────────────────────────────────────────────────┬─────────────────────────────────────┘
                                                   │ Protected Shared State (Mutex)
                                                   ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ [Thread 3: Periodic Logger / Monitor]                                                  │
│ Exact 1.0s interval (`clock_nanosleep`), samples state, computes CPU %, logs to file.  │
└──────────────────────────────────────────────────┬─────────────────────────────────────┘
                                                   │ Append CSV record
                                                   ▼
                                      ┌────────────────────────┐
                                      │    metrics_log.txt     │
                                      └────────────────────────┘
