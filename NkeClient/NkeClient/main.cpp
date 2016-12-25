//
//  main.cpp
//  NkeClient
//
//  Copyright (c) 2016 Slava Imameev. All rights reserved.
//

#include <iostream>
#include "NkeConnection.h"

//
// THIS IS A TEST APPLICATION.
// For a real world application a more accurate errors processing must be implemented.
//

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
    if( ret ){
        perror("pthread_create( SocketNotificationHandler )");
        nkeSocketThread = NULL;
    }
    
    if( nkeSocketThread )
        pthread_join(nkeSocketThread, (void **)&kr);
    
    IOServiceClose(connection);
    return 0;
}

//-------------------------------------------------------------

int min( int a, int b ) { return a<b ? a : b ;}

IOReturn
NkeSocketHandler(io_connect_t connection)
{
    kern_return_t       kr;
    uint32_t            dataSize;
    IODataQueueMemory  *queueMappedMemory;
    vm_size_t           queueMappedMemorySize;
    mach_vm_address_t   address = NULL;
    mach_vm_size_t      size = 0x0;
    mach_port_t         recvPort;
    mach_vm_address_t   sharedBuffers[ kt_NkeSocketBuffersNumber ];
    mach_vm_size_t      sharedBuffersSize[ kt_NkeSocketBuffersNumber ];
    
    //
    // allocate a Mach port to receive notifications from the IODataQueue
    //
    if( !( recvPort = IODataQueueAllocateNotificationPort() ) ){
        printf("failed to allocate notification port\n");
        kr = kIOReturnError;
        goto exit;
    }
    
    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
        sharedBuffers[ i ] = NULL;
        sharedBuffersSize[ i ] = 0;
    }
    
    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
      
        //
        // map the kernel data buffers into this process address space,
        // this will call clientMemoryForType() inside our user client class
        //
        kr = IOConnectMapMemory( connection,
                                 kt_NkeAclTypeSocketDataBase + i,
                                 mach_task_self(),
                                 &sharedBuffers[ i ],
                                 &sharedBuffersSize[ i ],
                                 kIOMapAnywhere );
        
        if( kr != kIOReturnSuccess ){
            printf("failed to map memory (%d)\n",kr);
            goto exit;
        }
    }

    
    //
    // this will call registerNotificationPort() inside our user client class
    //
    kr = IOConnectSetNotificationPort(connection, kt_NkeNotifyTypeSocketFilter, recvPort, 0);
    if( kr != kIOReturnSuccess ){
        
        printf("failed to register notification port (%d)\n", kr);
        goto exit;
    }
    
    //
    // map a buffer used to deliver events from the filter, a data queue is implemented over it,
    // this will call clientMemoryForType() inside our user client class,
    //
    kr = IOConnectMapMemory( connection,
                            kt_NkeNotifyTypeSocketFilter,
                            mach_task_self(),
                            &address,
                            &size,
                            kIOMapAnywhere );
    if( kr != kIOReturnSuccess ){
        printf("failed to map memory (%d)\n",kr);
        goto exit;
    }
    
    queueMappedMemory = (IODataQueueMemory *)address;
    queueMappedMemorySize = size;
    
    printf("before the while loop\n");
    
    //bool first_iteration = true;
    
    while( kIOReturnSuccess == IODataQueueWaitForAvailableData(queueMappedMemory, recvPort) ) {
        
        //first_iteration = false;
        //printf("a buffer has been received\n");//do not call as it stalls the queue!
        
        while( IODataQueueDataAvailable(queueMappedMemory) ){
            
            NkeSocketFilterNotification notification;
            dataSize = sizeof(notification);
            
            //
            // get the event descriptor
            //
            kr = IODataQueueDequeue(queueMappedMemory, &notification, &dataSize);
            if( kr == kIOReturnSuccess ){
                
                printf("NKE event: %s\n", NkeEventToString( notification.event ) );
                
                if( notification.event == NkeSocketFilterEventDataIn || notification.event == NkeSocketFilterEventDataOut ){
                    
                    //
                    // print the first bytes as a string, sometimes you can see a human readable data, like HTTP headers
                    //
                    if( notification.eventData.inputoutput.dataSize && notification.eventData.inputoutput.buffers[0] < kt_NkeSocketBuffersNumber ){
                        printf("%.*s\n", min(120, notification.eventData.inputoutput.dataSize), (char*)sharedBuffers[notification.eventData.inputoutput.buffers[0]]);
                    }
                    
                    //
                    // create a response
                    //
                    NkeSocketFilterServiceResponse   response;
                    bzero(&response, sizeof( response ));
                    
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
    
    for( int i = 0; i < kt_NkeSocketBuffersNumber; ++i ){
        
        if( ! sharedBuffers[ i ] )
            continue;
        
        kr = IOConnectUnmapMemory( connection,
                                   kt_NkeAclTypeSocketDataBase + i,
                                   mach_task_self(),
                                   sharedBuffers[ i ] );
        if( kr != kIOReturnSuccess ){
            printf("failed to unmap memory (%d)\n", kr);
        }
        
    }
    
    if( address ){
        
        kr = IOConnectUnmapMemory( connection,
                                  kt_NkeNotifyTypeSocketFilter,
                                  mach_task_self(),
                                  address );
        if( kr != kIOReturnSuccess ){
            printf("failed to unmap memory (%d)\n", kr);
        }
    }
    
    if( recvPort )
        mach_port_destroy(mach_task_self(), recvPort);
    
    return kr;
}

const char* NkeEventToString( NkeSocketFilterEvent event )
{
    switch( event )
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

