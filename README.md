# Bluetooth RFCOMM Echo Server

A simple, lightweight C program that establishes a Bluetooth RFCOMM (Radio Frequency Communication) server 

## Linux version
The server uses the Linux BlueZ stack. It listens for an incoming connection on channel 1, accepts data from a connected client, and echoes the exact data back to that client.

## ESP32/esp-idf version (WIP)
The server uses the esp-idf NimBLE stack (BLE GATT). It handles RX characteristic changes, and it echoes messages back as TX Characteristics

