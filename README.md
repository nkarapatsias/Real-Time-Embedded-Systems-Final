# Real-Time Embedded Systems — Final Project 2026

![Platform](https://img.shields.io/badge/Platform-Raspberry%20Pi-C51A4A?logo=raspberry-pi&logoColor=white)
![Language](https://img.shields.io/badge/Language-C99-00599C?logo=c&logoColor=white)
![Threading](https://img.shields.io/badge/Concurrency-POSIX%20Threads-blue)
![Architecture](https://img.shields.io/badge/Pattern-Producer--Consumer-success)

## 📌 Project Overview

This project implements a multi-threaded, real-time embedded telemetry ingestion and processing engine on Linux (targeting **Raspberry Pi Zero W** or equivalent SBC hardware).

The system subscribes asynchronously to the global **Bluesky Jetstream Firehose**, queues and parses incoming JSON events, maintains real-time statistics under strict mutual exclusion, and samples deterministic system metrics with sub-millisecond precision.

---

## 🎯 Target Stream Specification

The system taps into the public Bluesky Jetstream WebSocket feed over the AT Protocol:

* **Endpoint:** `wss://jetstream1.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post`
* **Payload:** Text-based JSON objects.
* **Filter Target:** Inspect and classify the `"kind"` property for each incoming record.

---

## 🏗️ System Architecture

The application is written strictly in **User-Space C** using a **Bounded Circular Buffer Producer-Consumer** pattern synchronized via POSIX threads (`pthreads`), mutexes (`pthread_mutex_t`), and condition variables (`pthread_cond_t`).
