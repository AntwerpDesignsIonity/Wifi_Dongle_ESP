/**
 * @file tusb_config.h
 * @brief TinyUSB device configuration for ESP32-S3 WiFi Dongle.
 *
 * Enables only the CDC-ECM/RNDIS network class so the dongle is recognised
 * as a USB Ethernet adapter on Windows (RNDIS) and Linux/macOS (CDC-ECM).
 */
#pragma once

/* -------------------------------------------------------------------------
 * MCU / OS / port
 * ---------------------------------------------------------------------- */
#define CFG_TUSB_MCU                OPT_MCU_ESP32S3
#define CFG_TUSB_OS                 OPT_OS_FREERTOS
/* Full-speed device on rhport 0 (the USB-OTG peripheral of the S3) */
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* -------------------------------------------------------------------------
 * Memory
 * ---------------------------------------------------------------------- */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

/* -------------------------------------------------------------------------
 * Control endpoint size (full-speed max = 64)
 * ---------------------------------------------------------------------- */
#define CFG_TUD_ENDPOINT0_SIZE      64

/* -------------------------------------------------------------------------
 * Enabled device classes
 * Only the network class is enabled; everything else is off to minimise
 * flash and RAM usage.
 * ---------------------------------------------------------------------- */
#define CFG_TUD_CDC                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/**
 * CFG_TUD_ECM_RNDIS = 1 → the device exposes a combined RNDIS + CDC-ECM
 * interface.  Windows selects the RNDIS interface; Linux/macOS prefer
 * CDC-ECM.  Both protocols carry raw Ethernet frames so the bridging code
 * is the same for either host OS.
 */
#define CFG_TUD_ECM_RNDIS           1
/** CDC-NCM is the newer standard but has less broad driver support on older
 *  Windows versions, so we keep it disabled here. */
#define CFG_TUD_NCM                 0

/* -------------------------------------------------------------------------
 * Network endpoint / buffer sizing
 * ---------------------------------------------------------------------- */
/** Maximum Ethernet frame size accepted / transmitted */
#define CFG_TUD_NET_MTU             1514U
/** Endpoint packet size for bulk transfers (full-speed max = 64) */
#define CFG_TUD_NET_ENDPOINT_SIZE   64U
