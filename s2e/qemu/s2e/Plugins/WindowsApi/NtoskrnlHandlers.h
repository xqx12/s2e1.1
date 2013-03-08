/*
 * S2E Selective Symbolic Execution Framework
 *
 * Copyright (c) 2010, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Currently maintained by:
 *    Vitaly Chipounov <vitaly.chipounov@epfl.ch>
 *    Volodymyr Kuznetsov <vova.kuznetsov@epfl.ch>
 *
 * All contributors are listed in S2E-AUTHORS file.
 *
 */

#ifndef S2E_PLUGINS_NTOSKRNLHANDLERS_H
#define S2E_PLUGINS_NTOSKRNLHANDLERS_H

#include <s2e/Plugin.h>
#include <s2e/Utils.h>
#include <s2e/Plugins/CorePlugin.h>
#include <s2e/S2EExecutionState.h>

#include <s2e/Plugins/FunctionMonitor.h>
#include <s2e/Plugins/ModuleExecutionDetector.h>
#include <s2e/Plugins/WindowsInterceptor/WindowsMonitor.h>
#include <s2e/Plugins/SymbolicHardware.h>

#include <s2e/Plugins/WindowsInterceptor/WindowsImage.h>

#include "Api.h"


namespace s2e {
namespace plugins {

#define NTOSKRNL_REGISTER_ENTRY_POINT(addr, ep) registerEntryPoint<NtoskrnlHandlers, NtoskrnlHandlers::EntryPoint>(state, this, &NtoskrnlHandlers::ep, addr);

class NtoskrnlHandlers: public WindowsApi
{
    S2E_PLUGIN
public:
    typedef void (NtoskrnlHandlers::*EntryPoint)(S2EExecutionState* state, FunctionMonitorState *fns);
    typedef std::map<std::string, NtoskrnlHandlers::EntryPoint> NtoskrnlHandlersMap;

    NtoskrnlHandlers(S2E* s2e): WindowsApi(s2e) {}

    void initialize();

public:
    static const WindowsApiHandler<EntryPoint> s_handlers[];
    static const NtoskrnlHandlersMap s_handlersMap;

private:
    bool m_loaded;
    ModuleDescriptor m_module;



    void onModuleLoad(
            S2EExecutionState* state,
            const ModuleDescriptor &module
            );

    void onModuleUnload(
        S2EExecutionState* state,
        const ModuleDescriptor &module
        );

    DECLARE_ENTRY_POINT(DebugPrint);
    DECLARE_ENTRY_POINT(IoCreateSymbolicLink);
    DECLARE_ENTRY_POINT(IoCreateDevice);
    DECLARE_ENTRY_POINT(IoIsWdmVersionAvailable);
    DECLARE_ENTRY_POINT_CO(IoFreeMdl);

    DECLARE_ENTRY_POINT(RtlEqualUnicodeString);
    DECLARE_ENTRY_POINT(RtlAddAccessAllowedAce);
    DECLARE_ENTRY_POINT(RtlCreateSecurityDescriptor);
    DECLARE_ENTRY_POINT(RtlSetDaclSecurityDescriptor);
    DECLARE_ENTRY_POINT(RtlAbsoluteToSelfRelativeSD);


    DECLARE_ENTRY_POINT(GetSystemUpTime);
    DECLARE_ENTRY_POINT(KeStallExecutionProcessor);

    DECLARE_ENTRY_POINT(ExAllocatePoolWithTag, uint32_t poolType, uint32_t size);

    static uint32_t IoGetCurrentIrpStackLocation(windows::IRP *Irp) {
        return ( (Irp)->Tail.Overlay.CurrentStackLocation );
    }

    static std::string readUnicodeString(S2EExecutionState *state, uint32_t pUnicodeString);
public:
    DECLARE_ENTRY_POINT_CALL(DriverDispatch, uint32_t irpMajor);
    DECLARE_ENTRY_POINT_RET(DriverDispatch, uint32_t irpMajor);
};

}


}

#endif
