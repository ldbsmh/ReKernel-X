#ifndef REKERNEL_X_UAPI_H
#define REKERNEL_X_UAPI_H

#include <cstdint>


#define REKERNEL_X_GENL_FAMILY_NAME       "rekernel_x2"
#define REKERNEL_X_GENL_VERSION           1
#define REKERNEL_X_GENL_MCGRP_NAME        "events"

#define REKERNEL_X_RPC_NAME_LEN           140

// netlink cmd
enum rekernel_x_genl_cmd
{
    REKERNEL_X_C_UNSPEC,
    REKERNEL_X_C_EVENT,			  // kernel -> user
    REKERNEL_X_C_ADD_MONITOR_NET, // user -> kernel
    REKERNEL_X_C_DEL_MONITOR_NET, // user -> kernel
    __REKERNEL_X_C_MAX,
};
#define REKERNEL_X_C_MAX (__REKERNEL_X_C_MAX - 1)

// netlink attr
enum rekernel_x_genl_attr
{
    REKERNEL_X_A_UNSPEC,

    // event
    REKERNEL_X_A_EVENT = 1, // nested

    // binder
    REKERNEL_X_A_BINDER = 10,			 // nested
    REKERNEL_X_A_BINDER_TYPE = 11,		 // s32
    REKERNEL_X_A_BINDER_ONEWAY = 12,	 // s32
    REKERNEL_X_A_BINDER_FROM_PID = 13,	 // s32
    REKERNEL_X_A_BINDER_FROM_UID = 14,	 // s32
    REKERNEL_X_A_BINDER_TARGET_PID = 15, // s32
    REKERNEL_X_A_BINDER_TARGET_UID = 16, // s32
    REKERNEL_X_A_BINDER_CODE = 17,		 // s32
    REKERNEL_X_A_BINDER_RPC_NAME = 18,	 // nul_str

    // signal
    REKERNEL_X_A_SIGNAL = 20,			 // nested
    REKERNEL_X_A_SIGNAL_SIGNAL = 21,	 // s32
    REKERNEL_X_A_SIGNAL_KILLER_PID = 22, // s32
    REKERNEL_X_A_SIGNAL_KILLER_UID = 23, // s32
    REKERNEL_X_A_SIGNAL_DST_PID = 24,	 // s32
    REKERNEL_X_A_SIGNAL_DST_UID = 25,	 // s32

    // network
    REKERNEL_X_A_NETWORK = 30,			  // nested
    REKERNEL_X_A_NETWORK_PROTO = 31,	  // s32
    REKERNEL_X_A_NETWORK_TARGET_UID = 32, // s32
    REKERNEL_X_A_NETWORK_DATA_LEN = 33,	  // s32

    REKERNEL_X_A_UID = 40, // u32

    __REKERNEL_X_A_MAX = 49,
};
#define REKERNEL_X_A_MAX __REKERNEL_X_A_MAX

// event types
enum rekernel_x_event_type
{
    REKERNEL_X_EVT_BINDER = 1,
    REKERNEL_X_EVT_SIGNAL = 2,
    REKERNEL_X_EVT_NETWORK = 3,
};

// binder type
enum rekernel_x_binder_type
{
    REKERNEL_X_BINDER_TRANSACTION = 1,
    REKERNEL_X_BINDER_REPLY = 2,
    REKERNEL_X_BINDER_FREE_BUFFER_FULL = 3,
};

// network protocol
enum rekernel_x_net_proto
{
    REKERNEL_X_NET_PROTO_IPV4 = 4,
    REKERNEL_X_NET_PROTO_IPV6 = 6,
};

#endif // REKERNEL_X_UAPI_H