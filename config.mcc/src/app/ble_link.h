/*******************************************************************************
  ble_link.h  -  replaces rnbd451.c/.h

  Glue between the on-chip BLE Transparent Profile Server (TRPS) and the slot
  car application.

  Threading model
    APP task (MCC app.c, owns the BLE stack):
        BLE_Link_OnConnected / OnDisconnected / OnTxEnabled / OnRxData
        BLE_Link_FlushTx       <- only place BLE_TRSPS_SendData() is called
    RACE task (race_task.c):
        BLE_Send, BLE_Link_Consume*, status getters

  Inbound protocol (phone -> car), ASCII, one command per BLE write or
  separated by CR / LF / ';' :
      START_RACE          start a RACE_NUM_LAPS race
      STOP_RACE           abort a running race / demo        (new)
      GHOST:<n>           n>0 start ghost mode, 0 stop
      PRACTICE:<n>        1 start practice, 0 stop
      DEMO                run the demo sequence (replaces button SW800) (new)
      ':' or ',' accepted as separator (same as RNBD451 build)

  Throttle is NOT carried over BLE: the two SAMD21 joystick remotes are polled
  over 433 MHz by the ATA8510 (ata8510.c).

  Outbound (car -> phone): <C1,12.345\n\r  <C2,..  <R1,pct\n\r  <R2,..
 *******************************************************************************/
#ifndef BLE_LINK_H
#define BLE_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LINK_CMD_MAX       (32U)   /* longest inbound command          */
#define BLE_LINK_TX_FRAME_MAX  (32U)   /* longest outbound frame           */
#define BLE_LINK_TX_DEPTH      (24U)   /* outbound frames buffered         */
#define BLE_LINK_CMD_DEPTH     (8U)    /* inbound commands buffered        */

/* ---- called once, before the scheduler starts or from APP_Initialize ---- */
void BLE_Link_Initialize(void);

/* ---- APP task side (call from the MCC-generated BLE event handlers) ---- */
void BLE_Link_OnConnected(uint16_t connHandle);
void BLE_Link_OnDisconnected(void);
void BLE_Link_OnTxEnabled(bool enabled);
void BLE_Link_OnRxData(const uint8_t *data, uint16_t len);
void BLE_Link_FlushTx(void);

/* Call from the MCC-generated APP_TrspsEvtHandler() (app_trsps_handler.c),
 * which exists once the Transparent Profile server is added in MCC. */
#if __has_include("ble_trsps/ble_trsps.h")
#include "ble_trsps/ble_trsps.h"
void BLE_Link_TrspsEvtHandler(BLE_TRSPS_Event_T *p_event);
#endif

/* Implemented in app.c (see app_integration_snippets.c): post an
 * APP_MSG_BLE_LINK_TX message to the APP task queue so FlushTx runs there. */
void BLE_Link_RequestFlush(void);

/* ---- RACE task side ---- */
bool BLE_IsConnected(void);
bool BLE_IsStreamOpen(void);              /* connected AND notifications on */
void BLE_Send(const char *msg, size_t len);
void BLE_Link_Kick(void);                 /* retry pending TX, call ~every 20 ms */

/* Fetch next inbound command into the "pending" slot if it is empty. */
void BLE_Link_Service(void);
/* Same semantics as RNBD451_ConsumeCommand / ...WithParam: consume pending
 * command if it matches. */
bool BLE_Link_ConsumeCommand(const char *cmd);
bool BLE_Link_ConsumeCommandWithParam(const char *cmd, uint8_t *param);
/* Discard an unmatched pending command (call at the end of a loop pass). */
void BLE_Link_DropPending(void);
/* True if a command is waiting (fetches one if the slot is empty). The
 * command stays pending; BLE_Link_PendingText() returns it ("" if none). */
bool BLE_Link_HasPending(void);
const char *BLE_Link_PendingText(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_LINK_H */
