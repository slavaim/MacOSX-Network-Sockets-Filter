/* 
 * Copyright (c) 2010 Slava Imameev. All rights reserved.
 */

#ifndef _NKEUSERTOKERNEL_H
#define _NKEUSERTOKERNEL_H

#include <sys/param.h>
#include <sys/kauth.h>
#include <sys/vnode.h>
#include <IOKit/scsi/SCSITask.h>
#include <netinet/in.h>

//
// ATTENTION!
// If the driver interface is changed the NkeDriverInterfaceVersion must be increased!
//

//--------------------------------------------------------------------

//
// because of 32 bit driver and 64 bit service the alignment and pack attribute is required for all structures
// that are shared by the service and the driver
//
#define NKE_ALIGNMENT  __attribute__((aligned(4),packed))

//--------------------------------------------------------------------

#define kNkeUserClientCookie     ( (UInt32)0xFFFABC15 )

//--------------------------------------------------------------------

enum {
    kt_NkeUserClientOpen = 0x0,             // 0x0
    kt_NkeUserClientClose,                  // 0x1
    kt_NkeUserClientSocketFilterResponse,   // 0x2
    
    //
    // the number of methods
    //
    kt_NkeUserClientMethodsMax,
    
    //
    // a fake method
    //
    kt_NkeStopListeningToMessages = 0xFFFFFFFF,
};

//--------------------------------------------------------------------

typedef enum{
    kt_NkeNotifyTypeUnknown = 0x0,
    kt_NkeNotifyTypeSocketFilter,
    
    //
    // always the last
    //
    kt_NkeNotifyTypeMax
    
    // but this is not the end of the story
    // [ kt_NkeAclTypeSocketDataBase, kt_NkeAclTypeSocketDataBase + kt_NkeSocketBuffersNumber ) is a range reserved for socket buffers!
    
} NkeNotifyType;

//
// there are kt_NkeSocketBuffersNumber buffer, this value is a starting base for IOConnectMapMemory,
// this out of range values can't be added to NkeNotifyType as kt_NkeNotifyTypeMax is used for a shared
// queue implementation
//
#define kt_NkeAclTypeSocketDataBase  (kt_NkeNotifyTypeMax+0x1000)

//--------------------------------------------------------------------

typedef struct _NkeSocketID
{
    //
    // a socket handle ( a socket address currently )
    //
    UInt64                      socket;
    
    //
    // a socket sequence to distinguish socket reusage and to not apply properties to reused socket
    //
    UInt64                      socketSequence;
    
} NkeSocketID;

typedef union _NkeSocketObjectAddress {
    struct sockaddr     hdr;
    struct sockaddr_in	addr4;		// ipv4 local addr
    struct sockaddr_in6	addr6;		// ipv6 local addr
} NkeSocketObjectAddress;

typedef enum _NkeSocketFilterEvent{
    NkeSocketFilterEventUnknown = 0x0,
    NkeSocketFilterEventConnected,
    NkeSocketFilterEventDisconnected,
    NkeSocketFilterEventShutdown,
    NkeSocketFilterEventCantrecvmore,
    NkeSocketFilterEventCantsendmore,
    NkeSocketFilterEventClosing,
    NkeSocketFilterEventBound,
    NkeSocketFilterEventDataIn,
    NkeSocketFilterEventDataOut,
    
    //
    // always the last, used to prevent the compiler from shrinking the enumerator size to 16 bytes
    //
    NkeSocketFilterEventMax = 0xFFFFFFFF
} NkeSocketFilterEvent;

//
// the notification layout is
//   NkeSocketFilterNotification
//   NkeSocketFilterEventXXXXXData
//   Data
//

typedef struct _NkeSocketFilterEventConnectedData{
    sa_family_t sa_family;
    NkeSocketObjectAddress localAddress;
	NkeSocketObjectAddress remoteAddress;
} NKE_ALIGNMENT NkeSocketFilterEventConnectedData;


typedef struct _NkeSocketFilterEventDisconnectedData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventDisconnectedData;


typedef struct _NkeSocketFilterEventShutdownData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventShutdownData;


typedef struct _NkeSocketFilterEventCantrecvmoreData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventCantrecvmoreData;


typedef struct _NkeSocketFilterEventCantsendmoreData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventCantsendmoreData;


typedef struct _NkeSocketFilterEventClosingData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventClosingData;


typedef struct _NkeSocketFilterEventBoundData{
    
    UInt32  reserved;
    
} NKE_ALIGNMENT NkeSocketFilterEventBoundData;

#define kt_NkeSocketBuffersNumber  40 // the maximum number 254
// static char StaticAssert_NkeSocketBuffersNumber[ (kt_NkeSocketBuffersNumber <= 254) ? 0 : -1 ];

#ifndef UINT8_MAX
    #define UINT8_MAX         255
#endif // UINT8_MAX

#ifndef UINT32_MAX
    #define UINT32_MAX        4294967295U
#endif // UINT32_MAX

typedef struct _NkeSocketFilterEventIoData{
    
    //
    // a full data size that follows this event,
    // a client must fetch all entries up to this
    // data size that follows this event
    //
    UInt32  dataSize;
    
    //
    // internal index, unique only for the socket, not uniqie across sockets
    //
    UInt32  dataIndex;
    
    //
    // data are placed in the buffers that are mapped by a call to IOConnectMapMemory
    // contains buffer indices, the terminating element has 0xFF value
    // if there is no terminating element the data occupies all buffers
    //
    UInt8   buffers[ kt_NkeSocketBuffersNumber ];
    
} NKE_ALIGNMENT NkeSocketFilterEventIoData;


typedef struct _NkeSocketFilterNotification{
    
    //
    // type of the event
    //
    NkeSocketFilterEvent   event;
    
    //
    // the full size of the notification structure, including all trailing data
    //
    UInt32                 size;
    
    //
    // a handle for a socket, an internal driver representation
    //
    NkeSocketID            socketId;
    
    //
    // union is to assist the compiler with data size and aligning
    //
    union{
        struct{
            UInt32   notificationForDisconnectedSocket: 0x1;
        } separated;
        
        UInt32  combined;
    } flags;
    
    //
    // event's data
    //
    union{
        NkeSocketFilterEventConnectedData       connected;
        NkeSocketFilterEventDisconnectedData    disconnected;
        NkeSocketFilterEventShutdownData        shutdown;
        NkeSocketFilterEventCantrecvmoreData    cantrecvmore;
        NkeSocketFilterEventCantsendmoreData    cantsendmore;
        NkeSocketFilterEventClosingData         closing;
        NkeSocketFilterEventBoundData           bound;
        NkeSocketFilterEventIoData              inputoutput; // NkeSocketFilterEventDataIn OR NkeSocketFilterEventDataOut
    }  eventData;
    
} NKE_ALIGNMENT NkeSocketFilterNotification;


typedef enum _NkeSocketDataPropertyType{
    NkeSocketDataPropertyTypeUnknown = 0x0,
    NkeSocketDataPropertyTypePermission = 0x1,
    
    //
    // just to help a compiler to infer data type
    //
    NkeSocketDataPropertyMax = UINT32_MAX
} NkeSocketDataPropertyType;

//--------------------------------------------------------------------

typedef enum _NkeCapturingMode{
    NkeCapturingModeInvalid = 0x0,    // an invalid value
    NkeCapturingModeAll = 0x1,        // capture all trafic
    NkeCapturingModeNothing = 0x2,    // do not capture, passthrough traffic
    NkeCapturingModeMax = UINT32_MAX  // to guide the compiler with type inferring
} NkeCapturingMode;

typedef struct _NkeSocketDataProperty{
    
    NkeSocketDataPropertyType   type;
    
    //
    // a data index as reported by NkeSocketFilterEventIoData
    //
    SInt32                      dataIndex;
    
    
    //
    // a handle for a socket as reported by NkeSocketFilterEventIoData
    //
    NkeSocketID                 socketId;
    
    //
    // a socket sequence to distinguish socket reusage and to not apply properties to reused socket
    //
    UInt64                      socketSequence;
    
    union{
        
        //
        // NkeSocketDataPropertyTypePermission
        //
        struct {
            uint8_t allowData;
        } permission;
        
    } value;
    
} NKE_ALIGNMENT NkeSocketDataProperty;

#define kt_NkeSocketDataPropertiesNumber kt_NkeSocketBuffersNumber

typedef struct _NkeSocketFilterServiceResponse
{
    //
    // if the type is NkeSocketDataPropertyTypeUnknown then the property is ignored
    // and the left properties are not being processed
    //
    NkeSocketDataProperty  property[ kt_NkeSocketDataPropertiesNumber ];
    
    //
    // the terminating value is UIN8_MAX or the entire array is processed
    //
    UInt8   buffersToRelease[ kt_NkeSocketBuffersNumber ];
    
} NKE_ALIGNMENT NkeSocketFilterServiceResponse;

#endif//_NKEUSERTOKERNEL_H

