# Bluetooth RFCOMM Echo Server

A simple, lightweight C program that establishes a Bluetooth RFCOMM (Radio Frequency Communication) server using the Linux BlueZ stack. It listens for an incoming connection on channel 1, accepts data from a connected client, and echoes the exact data back to that client.

## Features

* **Standard Socket API:** Uses familiar POSIX socket paradigms (`socket`, `bind`, `listen`, `accept`).
* **RFCOMM Protocol:** Emulates a serial port connection over Bluetooth.
* **Auto-Echo:** Displays incoming data locally and returns it directly to the sender.
* **Graceful Cleanup:** Closes server and client connections safely upon error or disconnection.

---

## Prerequisites

This program requires a Linux environment with the **BlueZ** Bluetooth stack and its development headers installed.

### Installing Dependencies

On Debian/Ubuntu-based systems, install the required library packages:

```bash
sudo apt update
sudo apt install build-essential libbluetooth-dev

On Archlinux based systems,

```bash
sudo pacman -S bluez bluez-utils python

### Configuring the bluetooth service
1. Enable the bluetooth service
sudo systemctl enable --now bluetooth

2. Configure bluetooth to be discorable and pairable
sudo bluetoothctl
>  power on
>  discoverable on
>  pairable on
>  exit

3. Configure SDP (Service Discovery Protocol) with a Serial Port Profile (SPP)
3.1 Legay 

sudo sdptool add SP

3.2 Current

sudo rfcomm listen /dev/rfcomm0 1
