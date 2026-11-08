//-----------------------------------------------------------------------------
//
//                   ** WARNING! ** 
//    This file was generated automatically by a tool.
//    Re-running the tool will overwrite this file.
//    You should copy this file to a custom location
//    before adding any customization in the copy to
//    prevent loss of your changes when the tool is
//    re-run.
//
//-----------------------------------------------------------------------------

#ifndef INTEROPLIB_INTEROPLIB_INTEROPLIB_DMXRECEIVER_H
#define INTEROPLIB_INTEROPLIB_INTEROPLIB_DMXRECEIVER_H

namespace interoplib
{
    namespace interoplib
    {
        struct DmxReceiver
        {
            // Helper Functions to access fields of managed object
            // Declaration of stubs. These functions are implemented by Interop code developers

            static signed int NativeStart( signed int param0, signed int param1, signed int param2, signed int param3, HRESULT &hr );

            static void NativeStop(  HRESULT &hr );

            static signed int NativeRead( uint16_t param0, CLR_RT_TypedArray_UINT8 param1, HRESULT &hr );

            static void NativeGetStats( CLR_RT_TypedArray_UINT32 param0, HRESULT &hr );

        };
    }
}

#endif // INTEROPLIB_INTEROPLIB_INTEROPLIB_DMXRECEIVER_H
