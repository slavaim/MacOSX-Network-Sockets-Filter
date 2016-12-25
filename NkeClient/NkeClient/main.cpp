//
//  main.cpp
//  NkeClient
//
//  Copyright (c) 2016 Slava Imameev. All rights reserved.
//

#include <iostream>
#include "NkeConnection.h"

//-------------------------------------------------------------

IOReturn
NkeSocketHandler(io_connect_t connection);
const char* NkeEventToString(NkeSocketFilterEvent   event);

//-------------------------------------------------------------

int main(int argc, const char * argv[])
{
    io_connect_t    connection;
    kern_return_t   kr;
    pthread_t       nkeSocketThread;
    
    kr = NkeOpenDlDriver( &connection );
    if( KERN_SUCCESS != kr ){
        
        return (-1);
    }

    int ret = pthread_create(&nkeSocketThread, (pthread_attr_t *)0,
                            (void* (*)(void*))NkeSocketHandler, (void *)connection);
    if (ret){
        perror("pthread_create( SocketNotificationHandler )");
        nkeSocketThread = NULL;
    }
    
    if( nkeSocketThread )
        pthread_join(nkeSocketThread, (void **)&kr);
    
    IOServiceClose(connection);
    return 0;
}

//-------------------------------------------------------------

IOReturn
NkeSocketHandler(io_connect_t connection)
{
    kern_return_t       kr;
    uint32_t            dataSize;
    IODataQueueMemory  *queueMappedMemory;
    vm_size_t           queueMappedMemorySize;
#if !__LP64__ || defined(IOCONNECT_MAPMEMORY_10_6)
    vm_address_t        address = nil;
    vm_size_t           size = 0;
#else
    mach_vm_address_t   address = NULL;
    mach_vm_size_t      size = 0x0;
#endif
    mach_port_t         recvPort;
    
    //
    // allocate a Mach port to receive notifications from the IODataQueue
    //
    if (!(recvPort = IODataQueueAllocateNotificationPort())) {
        printf("failed to allocate notification port\n");
        return kIOReturnError;
    }
    
    //
    // this will call registerNotificationPort() inside our user client class
    //
    kr = IOConnectSetNotificationPort(connection, kt_NkeNotifyTypeSocketFilter, recvPort, 0);
    if (kr != kIOReturnSuccess) {
        
        printf("failed to register notification port (%d)\n", kr);
        mach_port_destroy(mach_task_self(), recvPort);
        return kr;
    }
    
    //
    // this will call clientMemoryForType() inside our user client class
    //
    kr = IOConnectMapMemory( connection,
                            kt_NkeNotifyTypeSocketFilter,
                            mach_task_self(),
                            &address,
                            &size,
                            kIOMapAnywhere );
    if (kr != kIOReturnSuccess) {
        printf("failed to map memory (%d)\n",kr);
        mach_port_destroy(mach_task_self(), recvPort);
        return kr;
    }
    
    queueMappedMemory = (IODataQueueMemory *)address;
    queueMappedMemorySize = size;
    
    printf("before the while loop\n");
    
    //bool first_iteration = true;
    
    while( kIOReturnSuccess == IODataQueueWaitForAvailableData(queueMappedMemory, recvPort) ) {
        
        //first_iteration = false;
        //printf("a buffer has been received\n");//do not call as it stalls the queue!
        
        while (IODataQueueDataAvailable(queueMappedMemory)) {
            
            NkeSocketFilterNotification notification;
            dataSize = sizeof(notification);
            
            //
            // get the event header, the provided buffer is not big enough for data, so data will be jettisoned
            //
            kr = IODataQueueDequeue(queueMappedMemory, &notification, &dataSize);
            if (kr == kIOReturnSuccess) {
                
                printf("NKE event: %s\n", NkeEventToString( notification.event ) );
                
                if( notification.event == NkeSocketFilterEventDataIn || notification.event == NkeSocketFilterEventDataOut ){
                    
                    //
                    // create a response
                    //
                    NkeSocketFilterServiceResponse   response;
                    bzero( &response, sizeof( response ) );
                    
                    memcpy( response.buffersToRelease, notification.eventData.inputoutput.buffers, sizeof( response.buffersToRelease ) );
                    response.property[ 0 ].type = NkeSocketDataPropertyTypePermission;
                    response.property[ 0 ].socketId = notification.socketId;
                    response.property[ 0 ].dataIndex = notification.eventData.inputoutput.dataIndex;
                    response.property[ 0 ].value.permission.allowData = 0x1;
                    
                    response.property[ 1 ].type = NkeSocketDataPropertyTypeUnknown; // a terminating entry
                    
                    //
                    // send to the driver, we can block for a while here as the driver will inject data synchronously
                    //
                    size_t notUsed = sizeof(response);
                    
                    kr = IOConnectCallStructMethod( connection,
                                                   kt_NkeUserClientSocketFilterResponse,
                                                   (const void*)&response,
                                                   sizeof(response),
                                                   &response,
                                                   &notUsed );
                    if( kIOReturnSuccess != kr ){
                        
                        printf("IOConnectCallStructMethod failed with kr = 0x%X\n", kr);
                    }
                }

            } else {
                printf("IODataQueueDequeue failed with kr = 0x%X\\n", kr);
            }
            
        }// end while
        
    }// end while
    
exit:
    
    kr = IOConnectUnmapMemory( connection,
                              kt_NkeNotifyTypeSocketFilter,
                              mach_task_self(),
                              address );
    if (kr != kIOReturnSuccess){
        printf("failed to unmap memory (%d)\n", kr);
    }
    
    mach_port_destroy(mach_task_self(), recvPort);
    
    return kr;
}

const char* NkeEventToString( NkeSocketFilterEvent event )
{
    switch(event)
    {
        case NkeSocketFilterEventUnknown: return "NkeSocketFilterEventUnknown";
        case NkeSocketFilterEventConnected: return "NkeSocketFilterEventConnected";
        case NkeSocketFilterEventDisconnected: return "NkeSocketFilterEventDisconnected";
        case NkeSocketFilterEventShutdown: return "NkeSocketFilterEventShutdown";
        case NkeSocketFilterEventCantrecvmore: return "NkeSocketFilterEventCantrecvmore";
        case NkeSocketFilterEventCantsendmore: return "NkeSocketFilterEventCantsendmore";
        case NkeSocketFilterEventClosing: return "NkeSocketFilterEventClosing";
        case NkeSocketFilterEventBound: return "NkeSocketFilterEventBound";
        case NkeSocketFilterEventDataIn: return "NkeSocketFilterEventDataIn";
        case NkeSocketFilterEventDataOut: return "NkeSocketFilterEventDataOut";
        default: return "UNKNOWN";
    }
}

//-------------------------------------------------------------

