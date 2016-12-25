/* 
 * Copyright (c) 2010 Slava Imameev. All rights reserved.
 */

#include <IOKit/IODataQueueShared.h>
#include <sys/types.h>
#include <sys/vm.h> // for current_proc()
#include "NkeIOUserClient.h"
#include "NkeSocketFilter.h"

//--------------------------------------------------------------------

NkeIOUserClientRef          gServiceUserClient;

//--------------------------------------------------------------------

#define super IOUserClient

OSDefineMetaClassAndStructors( NkeIOUserClient, IOUserClient )

//--------------------------------------------------------------------

//#define kAny ((IOByteCount) -1 )
/*
 a call stack for a client's function invokation
 #11 0x465020f8 in NkeIOUserClient::setVidPidWhiteList (this=0xb530a00, vInBuffer=0x6f3d970, vOutBuffer=0x3189ac64, vInSize=0x24, vOutSizeP=0x3189aa70) at /work/DeviceLockProject/DeviceLockIOKitDriver/NkeIOUserClient.cpp:957
 #12 0x00647b7f in shim_io_connect_method_structureI_structureO (method=0x46549c30, object=0xb530a00, input=0x6f3d970 "", inputCount=36, output=0x3189ac64 "¼ÂTF¬1", outputCount=0x3189aa70) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/iokit/Kernel/IOUserClient.cpp:3592
 #13 0x0064c677 in IOUserClient::externalMethod (this=0xb530a00, selector=6, args=0x3189aaf0, dispatch=0x0, target=0x0, reference=0x0) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/iokit/Kernel/IOUserClient.cpp:4199
 #14 0x0064a368 in is_io_connect_method (connection=0xb530a00, selector=6, scalar_input=0x6f3d96c, scalar_inputCnt=0, inband_input=0x6f3d970 "", inband_inputCnt=36, ool_input=0, ool_input_size=0, scalar_output=0xb5815c8, scalar_outputCnt=0xb5815c4, inband_output=0x3189ac64 "¼ÂTF¬1", inband_outputCnt=0x3189ac60, ool_output=0, ool_output_size=0x6f3d9b4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/iokit/Kernel/IOUserClient.cpp:2745
 #15 0x002c1261 in _Xio_connect_method (InHeadP=0x6f3d944, OutHeadP=0xb5815a0) at device/device_server.c:15466
 #16 0x00226d74 in ipc_kobject_server (request=0x6f3d900) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/osfmk/kern/ipc_kobject.c:339
 #17 0x002126b1 in ipc_kmsg_send (kmsg=0x6f3d900, option=0, send_timeout=0) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/osfmk/ipc/ipc_kmsg.c:1371
 #18 0x0021e193 in mach_msg_overwrite_trap (args=0x3189bf60) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/osfmk/ipc/mach_msg.c:505
 #19 0x0021e37d in mach_msg_trap (args=0x3189bf60) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/osfmk/ipc/mach_msg.c:572
 #20 0x002d88fb in mach_call_munger (state=0x5c33d40) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/osfmk/i386/bsd_i386.c:697 
 */

static const IOExternalMethod sMethods[ kt_NkeUserClientMethodsMax ] =
{
    // 0x0 kt_NkeUserClientOpen
    {
        NULL,
        (IOMethod)&NkeIOUserClient::open,
        kIOUCScalarIScalarO,
        0,
        0
    },
    // 0x1 kt_NkeUserClientClose
    {
        NULL,
        (IOMethod)&NkeIOUserClient::close,
        kIOUCScalarIScalarO,
        0,
        0
    },
    // 0x2 kt_NkeUserClientSocketFilterResponse
    {
        NULL,
        (IOMethod)&NkeIOUserClient::processServiceSocketFilterResponse,
        kIOUCStructIStructO,
        kIOUCVariableStructureSize,
        kIOUCVariableStructureSize
    }

};

//--------------------------------------------------------------------

NkeIOUserClient* NkeIOUserClient::withTask( __in task_t owningTask )
{
    NkeIOUserClient* client;
    
    DBG_PRINT(("NkeIOUserClient::withTask( %p )\n", (void*)owningTask ));
    
    client = new NkeIOUserClient();
    if( !client )
        return NULL;
    
    //
    // set an invalid PID
    //
    client->fClientPID = (-1);
        
    if (client->init() == false) {
        
        client->release();
        return NULL;
    }
    
    for( int type = 0x0; type < kt_NkeNotifyTypeMax; ++type ){
        
        //
        // a default size is 512 Kb for each queue
        //
        client->fQueueSize[ type ] = 0x80000;
    }
    
    //
    // the network data notification, the data send in separate mapped buffers
    //
    client->fQueueSize[ kt_NkeNotifyTypeSocketFilter ] = 0x100000;
    
    client->fClient = owningTask;
    
    return client;
}

//--------------------------------------------------------------------

bool NkeIOUserClient::start( __in IOService *provider )
{
    this->fProvider = OSDynamicCast( NetworkKernelExtension, provider );
    assert( this->fProvider );
    if( !this->fProvider )
        return false;
    
    if( !super::start( provider ) ){
        
        DBG_PRINT_ERROR(("super::start(%p) failed\n", (void*)provider ));
        return false;
    }
    
    for( int type = 0x0; type < kt_NkeNotifyTypeMax; ++type ){
        
        //
        // a fake type doesn't require any resourves
        //
        if( kt_NkeNotifyTypeUnknown == type )
            continue;
        
        //
        // allocate a lock
        //
        this->fLock[ type ] = IOLockAlloc();
        assert( this->fLock[ type ] );
        if( !this->fLock[ type ] ){
            
            DBG_PRINT_ERROR(("this->fLock[ %u ]->IOLockAlloc() failed\n", type));
            
            super::stop( provider );
            return false;
        }        
        
        //
        // allocate a queue
        //
        this->fDataQueue[ type ] = IODataQueue::withCapacity( this->fQueueSize[ type ] );
        assert( this->fDataQueue[ type ] );
        while( !this->fDataQueue[ type ] && this->fQueueSize[ type ] > 0x8000){
            
            //
            // try to decrease the queue size until the low boundary of 32 Kb is reached
            //
            this->fQueueSize[ type ] = this->fQueueSize[ type ]/2;
            this->fDataQueue[ type ] = IODataQueue::withCapacity( this->fQueueSize[ type ]/2 );
        }
        
        assert( this->fDataQueue[ type ] );
        if( !this->fDataQueue[ type ] ){
            
            DBG_PRINT_ERROR(("this->fDataQueue[ %u ]->withCapacity( %u ) failed\n", type, (int)this->fQueueSize[ type ]));
            
            super::stop( provider );
            return false;
        }
        
        //
        // get the queue's memory descriptor
        //
        this->fSharedMemory[ type ] = this->fDataQueue[ type ]->getMemoryDescriptor();
        assert( this->fSharedMemory[ type ] );
        if( !this->fSharedMemory[ type ] ) {
            
            DBG_PRINT_ERROR(("this->fDataQueue[ %u ]->getMemoryDescriptor() failed\n", type));
            
            super::stop( provider );
            return false;
        }
        
    }// end for
    
    return true;
}

//--------------------------------------------------------------------

void NkeIOUserClient::freeAllocatedResources()
{
    
    for( int type = 0x0; type < kt_NkeNotifyTypeMax; ++type ){
        
        if( this->fDataQueue[ type ] ){
            
            //
            // send a termination notification to the user client
            //
            UInt32 message = kt_NkeStopListeningToMessages;
            this->fDataQueue[ type ]->enqueue(&message, sizeof(message));
        }
        
        if( this->fSharedMemory[ type ] ) {
            
            this->fSharedMemory[ type ]->release();
            this->fSharedMemory[ type ] = NULL;
        }
        
        if( this->fDataQueue[ type ] ){
            
            this->fDataQueue[ type ]->release();
            this->fDataQueue[ type ] = NULL;
        }
        
        if( this->fLock[ type ] ){
            
            IOLockFree( this->fLock[ type ] );
            this->fLock[ type ] = NULL;
        }
        
    }// end for
    
}

//--------------------------------------------------------------------

void NkeIOUserClient::free()
{
    this->freeAllocatedResources();
    
    super::free();
}

//--------------------------------------------------------------------

void NkeIOUserClient::stop( __in IOService *provider )
{
    this->freeAllocatedResources();
    
    super::stop( provider );
}

//--------------------------------------------------------------------

IOReturn NkeIOUserClient::open(void)
{
    if( this->isInactive() )
        return kIOReturnNotAttached;
    
    //
    // only one user client allowed
    //
    if( !fProvider->open(this) )
        return kIOReturnExclusiveAccess;
    
    this->fClientProc = current_proc();
    this->fClientPID  = proc_pid( current_proc() );
    
    return this->startLogging();
}

//--------------------------------------------------------------------

IOReturn NkeIOUserClient::clientClose(void)
{
    if( !this->clientClosedItself ){
        
        //
        // looks like the client process was aborted
        //
        if( this->fProvider ){
            
            if( this->fProvider->isOpen(this) )
                this->fProvider->close(this);
        }
        
        (void)this->close();
    }

    (void)this->terminate(0);
    
    this->fClient = NULL;
    this->fClientPID = (-1);
    this->fProvider = NULL;    
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

IOReturn NkeIOUserClient::close(void)
{
    
    if( !this->fProvider )
        return kIOReturnNotAttached;
    
    if( this->fProvider->isOpen(this) )
        this->fProvider->close(this);
    
    //
    // fix the fact that the client in a normal way of detaching
    //
    this->clientClosedItself = true;
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

bool NkeIOUserClient::terminate(IOOptionBits options)
{
    //
    // if somebody does a kextunload while a client is attached
    //
    if( this->fProvider && this->fProvider->isOpen(this) )
        this->fProvider->close(this);
    
    (void)stopLogging();
    
    return super::terminate( options );
}

//--------------------------------------------------------------------

IOReturn
NkeIOUserClient::startLogging(void)
{
    IOReturn   RC;
    
    assert( preemption_enabled() );
    assert( gSocketFilter );
    
    if( !gSocketFilter ){
        
        DBG_PRINT_ERROR(("An attempt to connect a client to a driver with missing global objects\n"));
        return kIOReturnInternalError;
    }
    
    RC = gServiceUserClient.registerUserClient( this );
    assert( kIOReturnSuccess == RC );
    if( kIOReturnSuccess != RC ){
        
        DBG_PRINT_ERROR(("gServiceUserClient.registerUserClient( this ) failed\n"));
        goto __exit;
    }
    
    RC = gSocketFilter->registerUserClient( this );
    assert( kIOReturnSuccess == RC );
    if( kIOReturnSuccess != RC ){
        
        DBG_PRINT_ERROR(("gSocketFilter->registerUserClient( this ) failed\n"));
        goto __exit;
    }
    
__exit:
    
    if( kIOReturnSuccess != RC ){
        gSocketFilter->unregisterUserClient( this );
    }
    
    return RC;
}

//--------------------------------------------------------------------

/*
 a call stack example
 #0  machine_switch_context (old=0x7ae9b7c, continuation=0, new=0x59f0000) at /SourceCache/xnu/xnu-1504.7.4/osfmk/i386/pcb.c:869
 #1  0x00226e57 in thread_invoke (self=0x7ae9b7c, thread=0x59f0000, reason=0) at /SourceCache/xnu/xnu-1504.7.4/osfmk/kern/sched_prim.c:1628
 #2  0x002270f6 in thread_block_reason (continuation=0, parameter=0x0, reason=<value temporarily unavailable, due to optimizations>) at /SourceCache/xnu/xnu-1504.7.4/osfmk/kern/sched_prim.c:1863
 #3  0x00227184 in thread_block (continuation=0) at /SourceCache/xnu/xnu-1504.7.4/osfmk/kern/sched_prim.c:1880
 #4  0x004869c0 in _sleep (chan=0x6382f8c "\001", pri=50, wmsg=<value temporarily unavailable, due to optimizations>, abstime=1084756243159, continuation=0, mtx=0x0) at /SourceCache/xnu/xnu-1504.7.4/bsd/kern/kern_synch.c:241
 #5  0x00486eef in msleep (chan=0x6382f8c, mtx=0x0, pri=50, wmsg=0x471b3854 "NkeSocketFilter::unregisterUserClient()", ts=0x32133d20) at /SourceCache/xnu/xnu-1504.7.4/bsd/kern/kern_synch.c:335
 #6  0x4718fbc0 in NkeSocketFilter::unregisterUserClient (this=0x6382f80, client=0x75d5f00) at /work/DL_MacSvn/mac/dl-0.x.beta/mac/dl-0.x/NKEriver/NKE/NkeSocketFilter.cpp:877
 #7  0x470ba8fb in NkeIOUserClient::stopLogging (this=0x75d5f00) at /work/DL_MacSvn/mac/dl-0.x.beta/mac/dl-0.x/NKEriver/NkeIOUserClient.cpp:514
 #8  0x470b7cc0 in NkeIOUserClient::terminate (this=0x75d5f00, options=0) at /work/DL_MacSvn/mac/dl-0.x.beta/mac/dl-0.x/NKEriver/NkeIOUserClient.cpp:408
 #9  0x47034b92 in NkeHookerCommonClass2<IOUserClientNkeHook<(_NkeInheritanceDepth)1>, IOUserClient>::terminate_hook (this=0x75d5f00, options=0) at NkeHookerCommonClass2.h:1117
 #10 0x470b7b5c in NkeIOUserClient::clientClose (this=0x75d5f00) at /work/DL_MacSvn/mac/dl-0.x.beta/mac/dl-0.x/NKEriver/NkeIOUserClient.cpp:346
 #11 0x00561555 in is_io_service_close (connection=0x75d5f00) at /SourceCache/xnu/xnu-1504.7.4/iokit/Kernel/IOUserClient.cpp:2443
 #12 0x002897d9 in _Xio_service_close (InHeadP=0x7135ea4, OutHeadP=0x5c6fb98) at device/device_server.c:3117
 #13 0x0021d7f7 in ipc_kobject_server (request=0x7135e00) at /SourceCache/xnu/xnu-1504.7.4/osfmk/kern/ipc_kobject.c:339
 #14 0x00210983 in ipc_kmsg_send (kmsg=0x7135e00, option=0, send_timeout=0) at /SourceCache/xnu/xnu-1504.7.4/osfmk/ipc/ipc_kmsg.c:1371
 #15 0x00216be6 in mach_msg_overwrite_trap (args=0x716d708) at /SourceCache/xnu/xnu-1504.7.4/osfmk/ipc/mach_msg.c:505
 #16 0x00293eb4 in mach_call_munger64 (state=0x716d704) at /SourceCache/xnu/xnu-1504.7.4/osfmk/i386/bsd_i386.c:830 
 */

IOReturn
NkeIOUserClient::stopLogging(void)
{
    
    this->fNotificationPorts[ kt_NkeNotifyTypeSocketFilter ] = 0x0;

    if( gSocketFilter )
        gSocketFilter->unregisterUserClient( this );
    
    gServiceUserClient.unregisterUserClient( this );
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------


IOReturn
NkeIOUserClient::registerNotificationPort( __in mach_port_t port, __in UInt32 type, __in UInt32 ref)
{    
    if ( (port == MACH_PORT_NULL) || 
         (UInt32)kt_NkeNotifyTypeUnknown == type || type >= (UInt32)kt_NkeNotifyTypeMax )
        return kIOReturnError;
    
    if( !fDataQueue[ type ] )
        return kIOReturnError;
        
    //
    // the order does matter ( may be a memory barrier is required )
    //
    this->fDataQueue[ type ]->setNotificationPort( port );
    this->fNotificationPorts[ type ] = port;
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

IOReturn
NkeIOUserClient::clientMemoryForType( __in UInt32 type, __in IOOptionBits *options,
                                      __in IOMemoryDescriptor **memory)
{
    *memory = NULL;
    *options = 0;
    
    //
    // check for socket data notification type
    //
    if( type >= (UInt32)kt_NkeAclTypeSocketDataBase && type < (UInt32)(kt_NkeAclTypeSocketDataBase + kt_NkeSocketBuffersNumber) ){
        
        if( ! gSocketFilter )
            return kIOReturnNoMemory;
        
        IOMemoryDescriptor* memoryDescr = gSocketFilter->getSocketBufferMemoryDescriptor( (UInt32)( type - kt_NkeAclTypeSocketDataBase ) );
        if( NULL == memoryDescr ){
            
            //
            // in most of the cases this is not an error, the buffer for the index doesn't exist,
            // but we cannot tell a caller that the buffer just doesn't exist so return an error,
            // a caller should ignor the error and continue execution
            //
            return kIOReturnNoDevice;
        }
        
        *memory = memoryDescr;
        return kIOReturnSuccess;
    }
    
    //
    // check for shared circular queue memory type
    //
    if( (UInt32)kt_NkeNotifyTypeUnknown != type && type < (UInt32)kt_NkeNotifyTypeMax ){
        
        assert( this->fSharedMemory[ type ] );
        if (!this->fSharedMemory[ type ])
            return kIOReturnNoMemory;
        
        //
        // client will decrement this reference
        //
        this->fSharedMemory[ type ]->retain();
        *memory = this->fSharedMemory[ type ];
        
        return kIOReturnSuccess;
    }
    
    //
    // the type is out of range
    //
    DBG_PRINT_ERROR(("memory type %d is out of range\n", (int)type));
    return kIOReturnBadArgument;
}

//--------------------------------------------------------------------

IOExternalMethod*
NkeIOUserClient::getTargetAndMethodForIndex( __in IOService **target, __in UInt32 index)
{
    if( index >= (UInt32)kt_NkeUserClientMethodsMax )
        return NULL;
    
    *target = this; 
    return (IOExternalMethod *)&sMethods[index];
}

//--------------------------------------------------------------------

volatile SInt32 memoryBarrier = 0x0;

void
NkeMemoryBarrier()
{
    /*
     "...locked operations serialize all outstanding load and store operations
     (that is, wait for them to complete)." ..."Locked operations are atomic
     with respect to all other memory operations and all externally visible events.
     Only instruction fetch and page table accesses can pass locked instructions.
     Locked instructions can be used to synchronize data written by one processor
     and read by another processor." - Intel® 64 and IA-32 Architectures Software Developer’s Manual, Chapter 8.1.2.
     */
    OSIncrementAtomic( &memoryBarrier );
}

//--------------------------------------------------------------------

class IODataQueueWrapper: public IODataQueue
{
public:
    
    Boolean enqueueWithBarrier(void * data, UInt32 dataSize)
    {
        const UInt32       head      = dataQueue->head;  // volatile
        const UInt32       tail      = dataQueue->tail;
        const UInt32       entrySize = dataSize + DATA_QUEUE_ENTRY_HEADER_SIZE;
        IODataQueueEntry * entry;
        
        assert( preemption_enabled() );
        
        if ( tail >= head )
        {
            // Is there enough room at the end for the entry?
            if ( (tail + entrySize) <= dataQueue->queueSize )
            {
                entry = (IODataQueueEntry *)((UInt8 *)dataQueue->queue + tail);
                
                entry->size = dataSize;
                memcpy(&entry->data, data, dataSize);
                
                // The tail can be out of bound when the size of the new entry
                // exactly matches the available space at the end of the queue.
                // The tail can range from 0 to dataQueue->queueSize inclusive.
                NkeMemoryBarrier();
                dataQueue->tail += entrySize;
            }
            else if ( head > entrySize ) 	// Is there enough room at the beginning?
            {
                // Wrap around to the beginning, but do not allow the tail to catch
                // up to the head.
                
                dataQueue->queue->size = dataSize;
                
                // We need to make sure that there is enough room to set the size before
                // doing this. The user client checks for this and will look for the size
                // at the beginning if there isn't room for it at the end.
                
                if ( ( dataQueue->queueSize - tail ) >= DATA_QUEUE_ENTRY_HEADER_SIZE )
                {
                    ((IODataQueueEntry *)((UInt8 *)dataQueue->queue + tail))->size = dataSize;
                }
                
                memcpy(&dataQueue->queue->data, data, dataSize);
                NkeMemoryBarrier();
                dataQueue->tail = entrySize;
            }
            else
            {
                return false;	// queue is full
            }
        }
        else
        {
            // Do not allow the tail to catch up to the head when the queue is full.
            // That's why the comparison uses a '>' rather than '>='.
            
            if ( (head - tail) > entrySize )
            {
                entry = (IODataQueueEntry *)((UInt8 *)dataQueue->queue + tail);
                
                entry->size = dataSize;
                memcpy(&entry->data, data, dataSize);
                NkeMemoryBarrier();
                dataQueue->tail += entrySize;
            }
            else
            {
                return false;	// queue is full
            }
        }
        
        // Send notification (via mach message) that data is available.
        
        if ( ( head == tail )                /* queue was empty prior to enqueue() */
            ||   ( dataQueue->head == tail ) )   /* queue was emptied during enqueue() */
        {
            sendDataAvailableNotification();
        }
        
        return true;
    }
    
    void forceSendDataAvailableNotification( UInt32 msgSize )
    {
        sendDataAvailableNotification();
    };
};

//--------------------------------------------------------------------

IOReturn NkeIOUserClient::socketFilterNotification( __in NkeSocketFilterNotification* data )
{
    assert( preemption_enabled() );
    assert( this->fDataQueue[ kt_NkeNotifyTypeSocketFilter ] );
    assert( this->fLock[ kt_NkeNotifyTypeSocketFilter ] );
    
    //
    // enqueue must be able to send a message to a client or else
    // the client will wait until the queue is full to receive
    // a message ( this is an internal enqueue logic ), the return
    // status for the socket notification is not a successful one 
    // as opposite to other notifications as returning a success will result
    // in failing to release data buffers
    // 
#ifndef DBG
    if( 0x0 == this->fNotificationPorts[ kt_NkeNotifyTypeSocketFilter ] )
        return kIOReturnError;
#endif
    
    bool enqueued;
    
    //
    // the function is called from an arbitrary context, so the access
    // serialization to the queue is required, the lock must not
    // disable the preemtion as IODataQueue::sendDataAvailableNotification
    // can block on the mutex
    //
    IOLockLock( this->fLock[ kt_NkeNotifyTypeSocketFilter ] );
    {// start of the lock
        
        assert( preemption_enabled() );
        enqueued = ((IODataQueueWrapper*)this->fDataQueue[ kt_NkeNotifyTypeSocketFilter ])->enqueueWithBarrier( data, data->size );
#if defined( DBG )
        /*if( !enqueued ){
         
         __asm__ volatile( "int $0x3" );
         }*/
#endif//DBG
        
    }//end of the lock
    IOLockUnlock( this->fLock[ kt_NkeNotifyTypeSocketFilter ] );
    
    //assert( enqueued );
    if( !enqueued ){
        
        DBG_PRINT_ERROR(("this->fDataQueue[ kt_NkeNotifyTypeSocketFilter ]->enqueue failed\n"));
        
    }//end if( !enqueued )
    
    return enqueued? kIOReturnSuccess: kIOReturnNoMemory;
}

//--------------------------------------------------------------------

//
// alocates a memory in the kernel mode and copies a content of a user mode memory, the allocated memory is of
// the same size as provided by the second parameter, the memory is allocated by a call to IOMalloc, a caller must free
//
void* NkeIOUserClient::userModeMemoryToKernelMode(
    __in mach_vm_address_t  userAddress,
    __in mach_vm_size_t bufferSize
    )
{
    if( 0x0 == bufferSize || bufferSize > 1000*PAGE_SIZE )
        return NULL;
    
    void* kernelBuffer = IOMalloc( bufferSize );
    assert( kernelBuffer );
    if( !kernelBuffer ){
        
        DBG_PRINT_ERROR(("kernelBuffer = IOMalloc( bufferSize ) failed\n"));
        return NULL;
    }
    
    IOReturn error = copyin( userAddress , kernelBuffer, bufferSize );
    assert( !error );
    if( error ){
        
        DBG_PRINT_ERROR(( "copyin( userAddress , kernelBuffer, bufferSize ) failed with the 0x%x error\n", error ));
        IOFree( kernelBuffer, bufferSize );
        return NULL;
    }
    
    return kernelBuffer;
}

//--------------------------------------------------------------------

IOReturn
NkeIOUserClient::processServiceSocketFilterResponse(
    __in  void *vInBuffer, // NkeSocketFilterServiceResponse
    __out void *vOutBuffer,
    __in  void *vInSize,
    __in  void *vOutSizeP,
    void *, void *)
{
    NkeSocketFilterServiceResponse*  serviceSocketFilterResponse = (NkeSocketFilterServiceResponse*)vInBuffer;
    vm_size_t             inSize = (vm_size_t)vInSize;
    
    //
    // there is no output data
    //
    *(UInt32*)vOutSizeP = 0x0;
    
    if( inSize < sizeof( *serviceSocketFilterResponse ) ){
        
        DBG_PRINT_ERROR(("inSize < sizeof(*serviceResponse)\n"));
        return kIOReturnBadArgument;
    }
    
    if( ! gSocketFilter ){
        
        DBG_PRINT_ERROR(("gSocketFilter is NULL\n"));
        return kIOReturnBadArgument;
    }
    
    return gSocketFilter->processServiceResponse( serviceSocketFilterResponse );
}

//--------------------------------------------------------------------

IOReturn
NkeIOUserClientRef::registerUserClient( __in NkeIOUserClient* client )
{
    bool registered;
    
    if( this->pendingUnregistration ){
        
        DBG_PRINT_ERROR(("this->pendingUnregistration\n"));
        return kIOReturnError;
    }
    
    registered = OSCompareAndSwapPtr( NULL, (void*)client, &this->userClient );
    assert( registered );
    if( !registered ){
        
        DBG_PRINT_ERROR(("!registered\n"));
        return kIOReturnError;
    }
    
    client->retain();
    
    return registered? kIOReturnSuccess: kIOReturnError;
}

//--------------------------------------------------------------------

IOReturn
NkeIOUserClientRef::unregisterUserClient( __in NkeIOUserClient* client )
{
    bool   unregistered;
    NkeIOUserClient*  currentClient;
    
    currentClient = (NkeIOUserClient*)this->userClient;
    assert( currentClient == client );
    if( currentClient != client ){
        
        DBG_PRINT_ERROR(("currentClient != client\n"));
        return kIOReturnError;
    }
    
    this->pendingUnregistration = true;
    
    unregistered = OSCompareAndSwapPtr( (void*)currentClient, NULL, &this->userClient );
    assert( unregistered && NULL == this->userClient );
    if( !unregistered ){
        
        DBG_PRINT_ERROR(("!unregistered\n"));
        
        this->pendingUnregistration = false;
        return kIOReturnError;
    }
    
    do { // wait for any existing client invocations to return
        
        struct timespec ts = { 1, 0 }; // one second
        (void)msleep( &this->clientInvocations,      // wait channel
                     NULL,                          // mutex
                     PUSER,                         // priority
                     "NkeSocketFilter::unregisterUserClient()", // wait message
                     &ts );                         // sleep interval
        
    } while( this->clientInvocations != 0 );
    
    currentClient->release();
    this->pendingUnregistration = false;
    
    return unregistered? kIOReturnSuccess: kIOReturnError;
}

//--------------------------------------------------------------------

bool
NkeIOUserClientRef::isUserClientPresent()
{
    return ( NULL != this->userClient );
}

//--------------------------------------------------------------------

//
// if non NULL value is returned a caller must call releaseUserClient()
// when it finishes with the returned client object
//
NkeIOUserClient*
NkeIOUserClientRef::getUserClient()
{
    NkeIOUserClient*  currentClient;
    
    //
    // if ther is no user client, then nobody call for logging
    //
    if( NULL == this->userClient || this->pendingUnregistration )
        return kIOReturnSuccess;
    
    OSIncrementAtomic( &this->clientInvocations );
    
    currentClient = (NkeIOUserClient*)this->userClient;
    
    //
    // if the current client is NULL or can't be atomicaly exchanged
    // with the same value then the unregistration is in progress,
    // the call to OSCompareAndSwapPtr( NULL, NULL, &this->userClient )
    // checks the this->userClient for NULL atomically 
    //
    if( !currentClient ||
       !OSCompareAndSwapPtr( currentClient, currentClient, &this->userClient ) ||
       OSCompareAndSwapPtr( NULL, NULL, &this->userClient ) ){
        
        //
        // the unregistration is in the progress and waiting for all
        // invocations to return
        //
        assert( this->pendingUnregistration );
        if( 0x1 == OSDecrementAtomic( &this->clientInvocations ) ){
            
            //
            // this was the last invocation
            //
            wakeup( &this->clientInvocations );
        }
        
        return NULL;
    }
    
    return currentClient;
}

//--------------------------------------------------------------------

void
NkeIOUserClientRef::releaseUserClient()
{
    //
    // do not exchange or add any condition before OSDecrementAtomic as it must be always done!
    //
    if( 0x1 == OSDecrementAtomic( &this->clientInvocations ) && NULL == this->userClient ){
        
        //
        // this was the last invocation
        //
        wakeup( &this->clientInvocations );
    }
}

//--------------------------------------------------------------------

pid_t
NkeIOUserClientRef::getUserClientPid()
{
    NkeIOUserClient* client = this->getUserClient();
    if( ! client )
        return (-1);
    
    pid_t pid = client->getPid();
    this->releaseUserClient();
    
    return pid;
}

//--------------------------------------------------------------------
