/*
 *  NkeIOUserClientRef.h
 *  DeviceLock
 *
 *  Created by Slava on 2/01/13.
 *  Copyright 2013 Slava Imameev. All rights reserved.
 *
 */

#ifndef _NKEIOUSERCLIENTREF_H
#define _NKEIOUSERCLIENTREF_H

#include <IOKit/IOTypes.h>

//--------------------------------------------------------------------

class NkeIOUserClient;

class NkeIOUserClientRef{
    
protected:
    //
    // the user client's state variables
    //
    bool    pendingUnregistration;
    UInt32  clientInvocations;
    
    //
    // a user client for the kernel-to-user communication
    // the object is retained
    //
    volatile class NkeIOUserClient* userClient;
    
public:
    
    NkeIOUserClientRef(): pendingUnregistration(false), clientInvocations(0), userClient(NULL){;}
    virtual ~NkeIOUserClientRef(){ assert( NULL == userClient );}
    
    virtual bool isUserClientPresent();
    virtual IOReturn registerUserClient( __in NkeIOUserClient* client );
    virtual IOReturn unregisterUserClient( __in NkeIOUserClient* client );
    
    //
    // returns (-1) if there is no client
    //
    virtual pid_t getUserClientPid();
    
    //
    // a caller must call releaseUserClient() for each successfull call to getUserClient()
    //
    NkeIOUserClient* getUserClient();
    void releaseUserClient();
    
};

//--------------------------------------------------------------------

//
// a user client for the kernel-to-user communication
//
extern NkeIOUserClientRef       gServiceUserClient;

//--------------------------------------------------------------------
#endif // _NKEIOUSERCLIENTREF_H
