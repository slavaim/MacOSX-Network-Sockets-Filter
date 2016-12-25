/*
 * Copyright (c) 2016 Slava Imameev. All rights reserved.
 */

#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSContainers.h>
#include <IOKit/assert.h>
#include <IOKit/IOCatalogue.h>
#include "NkeCommon.h"
#include "NetworkKernelExtension.h"
#include "NkeSocketFilter.h"

//--------------------------------------------------------------------

NetworkKernelExtension* NetworkKernelExtension::Instance;
NkeSocketFilter*     gSocketFilter;

//--------------------------------------------------------------------

//
// the standard IOKit declarations
//
#undef super
#define super IOService

OSDefineMetaClassAndStructors(NetworkKernelExtension, IOService)

//--------------------------------------------------------------------

bool
NetworkKernelExtension::start(
                     __in IOService *provider
                     )
{
    
    Instance = this;
    
    //
    // start network filtering
    //
    if( KERN_SUCCESS == NkeSocketFilter::InitSocketFilterSubsystem() ){
        
        gSocketFilter = NkeSocketFilter::withDefault();
        assert( gSocketFilter );
        if( NULL == gSocketFilter ){
            
            DBG_PRINT_ERROR(("NkeSocketFilter::withDefault() fauled\n"));
            goto __exit_on_error;
        }
        
        DBG_PRINT(("NkeSocketFilter::withDefault() returned success gSocketFilter=0x%p\n", (void*)gSocketFilter ));
        
        gSocketFilter->startFilter();
        
    } else {
        
        DBG_PRINT_ERROR(("NkeSocketFilter::InitSocketFilterSubsystem() fauled\n"));
        goto __exit_on_error;
        
    }
    
    //
    // register with IOKit to allow the class matching
    //
    registerService();
    
    //
    // make the driver non-unloadable
    //
    // this->retain();
    
    return true;
    
__exit_on_error:
    
    //
    // all cleanup will be done in stop() and free()
    //
    this->release();
    return false;
}

//--------------------------------------------------------------------

void
NetworkKernelExtension::stop(
                    __in IOService * provider
                    )
{
    if( gSocketFilter ){
        
        gSocketFilter->stopFilter();
        gSocketFilter->release();
        gSocketFilter = NULL;
    }
    
    super::stop( provider );
}

//--------------------------------------------------------------------

bool NetworkKernelExtension::init()
{
    if(! super::init() )
        return false;
    
    return true;
}

//--------------------------------------------------------------------

//
// actually this will not be called as the module should be unloadable in release build
//
void NetworkKernelExtension::free()
{
    
    super::free();
}

//--------------------------------------------------------------------

IOReturn
NetworkKernelExtension::newUserClient(
    __in task_t owningTask,
    __in void * securityID,
    __in UInt32 type,
    __in IOUserClient **handler
    )
{
    
    NkeIOUserClient*  client = NULL;
    
    //
    // Check that this is a user client type that we support.
    // type is known only to this driver's user and kernel
    // classes. It could be used, for example, to define
    // read or write privileges. In this case, we look for
    // a private value.
    //
    if( type != kNkeUserClientCookie )
        return kIOReturnBadArgument;
    
    //
    // Construct a new client instance for the requesting task.
    // This is, essentially  client = new NkeIOUserClient;
    //                               ... create metaclasses ...
    //                               client->setTask(owningTask)
    //
    client = NkeIOUserClient::withTask( owningTask );
    assert( client );
    if( client == NULL ){
        
        DBG_PRINT_ERROR(("Can not create a user client for the task = %p\n", (void*)owningTask ));
        return kIOReturnNoResources;
    }
    
    //
    // Attach the client to our driver
    //
    if( !client->attach( this ) ) {
        
        assert( !"client->attach( this ) failed" );
        DBG_PRINT_ERROR(("Can attach a user client for the task = %p\n", (void*)owningTask ));
        
        client->release();
        return kIOReturnNoResources;
    }
    
    //
    // Start the client so it can accept requests
    //
    if( !client->start( this ) ){
        
        assert( !"client->start( this ) failed" );
        DBG_PRINT_ERROR(("Can start a user client for the task = %p\n", (void*)owningTask ));
        
        client->detach( this );
        client->release();
        return kIOReturnNoResources;
    }
    
    *handler = client;
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------


