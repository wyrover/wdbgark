/*
    * WinDBG Anti-RootKit extension
    * Copyright � 2013-2015  Vyacheslav Rusakoff
    * 
    * This program is free software: you can redistribute it and/or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    * (at your option) any later version.
    * 
    * This program is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    * GNU General Public License for more details.
    * 
    * You should have received a copy of the GNU General Public License
    * along with this program.  If not, see <http://www.gnu.org/licenses/>.

    * This work is licensed under the terms of the GNU GPL, version 3.  See
    * the COPYING file in the top-level directory.
*/

#include <string>
#include <algorithm>
#include <memory>

#include "wdbgark.hpp"
#include "manipulators.hpp"
#include "resources.hpp"

EXT_DECLARE_GLOBALS();

bool WDbgArk::Init() {
    if ( IsInited() )
        return true;

    m_is_cur_machine64 = IsCurMachine64();

    m_Symbols->Reload("");  // revise debuggee modules list

    if ( !CheckSymbolsPath(MS_PUBLIC_SYMBOLS_SERVER, true) )
        warn << __FUNCTION__ ": CheckSymbolsPath failed" << endlwarn;

    if ( !InitDummyPdbModule() )
        warn << __FUNCTION__ ": InitDummyPdbModule failed" << endlwarn;

    m_obj_helper = std::unique_ptr<WDbgArkObjHelper>(new WDbgArkObjHelper);
    m_color_hack = std::unique_ptr<WDbgArkColorHack>(new WDbgArkColorHack);

    // get system version
    HRESULT result = m_Control->GetSystemVersion(reinterpret_cast<PULONG>(&m_platform_id),
                                                 reinterpret_cast<PULONG>(&m_major_build),
                                                 reinterpret_cast<PULONG>(&m_minor_build),
                                                 NULL,
                                                 0,
                                                 NULL,
                                                 reinterpret_cast<PULONG>(&m_service_pack_number),
                                                 NULL,
                                                 0,
                                                 NULL);

    if ( !SUCCEEDED(result) )
        warn << __FUNCTION__ ": GetSystemVersion failed with result = " << result << endlwarn;

    InitCallbackCommands();
    InitCalloutNames();
    InitGDTSelectors();

    if ( !FindDbgkLkmdCallbackArray() )
        warn << __FUNCTION__ ": FindDbgkLkmdCallbackArray failed" << endlwarn;

    m_inited = true;

    return m_inited;
}

void WDbgArk::InitCallbackCommands(void) {
    // TODO(swwwolf): optimize by calculating offsets in constructor only once
    // init systemcb map
    SystemCbCommand command_info = { "nt!PspLoadImageNotifyRoutineCount", "nt!PspLoadImageNotifyRoutine", 0 };
    m_system_cb_commands["image"] = command_info;

    command_info.list_count_name = "nt!PspCreateProcessNotifyRoutineCount";
    command_info.list_head_name = "nt!PspCreateProcessNotifyRoutine";
    m_system_cb_commands["process"] = command_info;

    command_info.list_count_name = "nt!PspCreateThreadNotifyRoutineCount";
    command_info.list_head_name = "nt!PspCreateThreadNotifyRoutine";
    m_system_cb_commands["thread"] = command_info;

    command_info.list_count_name = "nt!CmpCallBackCount";
    command_info.list_head_name = "nt!CmpCallBackVector";
    command_info.offset_to_routine = GetCmCallbackItemFunctionOffset();
    m_system_cb_commands["registry"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!KeBugCheckCallbackListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["bugcheck"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!KeBugCheckReasonCallbackListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["bugcheckreason"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!KeBugCheckAddPagesCallbackListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["bugcheckaddpages"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!PopRegisteredPowerSettingCallbacks";
    command_info.offset_to_routine = GetPowerCallbackItemFunctionOffset();
    m_system_cb_commands["powersetting"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name.clear();
    command_info.offset_to_routine = 0;
    m_system_cb_commands["callbackdir"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopNotifyShutdownQueueHead";
    m_system_cb_commands["shutdown"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopNotifyLastChanceShutdownQueueHead";
    m_system_cb_commands["shutdownlast"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopDriverReinitializeQueueHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY") + m_PtrSize;
    m_system_cb_commands["drvreinit"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopBootDriverReinitializeQueueHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY") + m_PtrSize;
    m_system_cb_commands["bootdrvreinit"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopFsNotifyChangeQueueHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY") + m_PtrSize;
    m_system_cb_commands["fschange"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!KiNmiCallbackListHead";
    command_info.offset_to_routine = m_PtrSize;
    m_system_cb_commands["nmi"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!SeFileSystemNotifyRoutinesHead";
    command_info.offset_to_routine = m_PtrSize;
    m_system_cb_commands["logonsessionroutine"] = command_info;

    command_info.list_count_name = "nt!IopUpdatePriorityCallbackRoutineCount";
    command_info.list_head_name = "nt!IopUpdatePriorityCallbackRoutine";
    command_info.offset_to_routine = 0;
    m_system_cb_commands["prioritycallback"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name.clear();
    m_system_cb_commands["pnp"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!PspLegoNotifyRoutine";    // actually just a pointer
    m_system_cb_commands["lego"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!RtlpDebugPrintCallbackList";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["debugprint"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!AlpcpLogCallbackListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["alpcplog"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!EmpCallbackListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_GUID");
    m_system_cb_commands["empcb"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!IopPerfIoTrackingListHead";
    command_info.offset_to_routine = GetTypeSize("nt!_LIST_ENTRY");
    m_system_cb_commands["ioperf"] = command_info;

    command_info.list_count_name.clear();
    command_info.list_head_name = "nt!DbgkLkmdCallbackArray";
    command_info.offset_to_routine = 0;
    m_system_cb_commands["dbgklkmd"] = command_info;
}

void WDbgArk::InitCalloutNames(void) {
    if ( m_minor_build < W8RTM_VER ) {
        m_callout_names.push_back("nt!PspW32ProcessCallout");
        m_callout_names.push_back("nt!PspW32ThreadCallout");
        m_callout_names.push_back("nt!ExGlobalAtomTableCallout");
        m_callout_names.push_back("nt!KeGdiFlushUserBatch");
        m_callout_names.push_back("nt!PopEventCallout");
        m_callout_names.push_back("nt!PopStateCallout");
        m_callout_names.push_back("nt!PspW32JobCallout");
        m_callout_names.push_back("nt!ExDesktopOpenProcedureCallout");
        m_callout_names.push_back("nt!ExDesktopOkToCloseProcedureCallout");
        m_callout_names.push_back("nt!ExDesktopCloseProcedureCallout");
        m_callout_names.push_back("nt!ExDesktopDeleteProcedureCallout");
        m_callout_names.push_back("nt!ExWindowStationOkToCloseProcedureCallout");
        m_callout_names.push_back("nt!ExWindowStationCloseProcedureCallout");
        m_callout_names.push_back("nt!ExWindowStationDeleteProcedureCallout");
        m_callout_names.push_back("nt!ExWindowStationParseProcedureCallout");
        m_callout_names.push_back("nt!ExWindowStationOpenProcedureCallout");
        m_callout_names.push_back("nt!IopWin32DataCollectionProcedureCallout");
        m_callout_names.push_back("nt!PopWin32InfoCallout");
    }
}

void WDbgArk::InitGDTSelectors(void) {
    if ( m_is_cur_machine64 ) {
        m_gdt_selectors.push_back(KGDT64_NULL);
        m_gdt_selectors.push_back(KGDT64_R0_CODE);
        m_gdt_selectors.push_back(KGDT64_R0_DATA);
        m_gdt_selectors.push_back(KGDT64_R3_CMCODE);
        m_gdt_selectors.push_back(KGDT64_R3_DATA);
        m_gdt_selectors.push_back(KGDT64_R3_CODE);
        m_gdt_selectors.push_back(KGDT64_SYS_TSS);
        m_gdt_selectors.push_back(KGDT64_R3_CMTEB);
    } else {
        m_gdt_selectors.push_back(KGDT_R0_CODE);
        m_gdt_selectors.push_back(KGDT_R0_DATA);
        m_gdt_selectors.push_back(KGDT_R3_CODE);
        m_gdt_selectors.push_back(KGDT_R3_DATA);
        m_gdt_selectors.push_back(KGDT_TSS);
        m_gdt_selectors.push_back(KGDT_R0_PCR);
        m_gdt_selectors.push_back(KGDT_R3_TEB);
        m_gdt_selectors.push_back(KGDT_LDT);
        m_gdt_selectors.push_back(KGDT_DF_TSS);
        m_gdt_selectors.push_back(KGDT_NMI_TSS);
        m_gdt_selectors.push_back(KGDT_GDT_ALIAS);
        m_gdt_selectors.push_back(KGDT_CDA16);
        m_gdt_selectors.push_back(KGDT_CODE16);
        m_gdt_selectors.push_back(KGDT_STACK16);
    }
}

bool WDbgArk::CheckSymbolsPath(const std::string& test_path, const bool display_error) {
    unsigned __int32 buffer_size = 0;
    bool             result      = false;

    HRESULT hresult = m_Symbols->GetSymbolPath(nullptr, 0, reinterpret_cast<PULONG>(&buffer_size));

    if ( !SUCCEEDED(hresult) ) {
        err << __FUNCTION__ ": GetSymbolPath failed" << endlerr;
        return false;
    }

    std::unique_ptr<char[]> symbol_path_buffer(new char[buffer_size]);

    hresult = m_Symbols->GetSymbolPath(symbol_path_buffer.get(),
                                       buffer_size,
                                       reinterpret_cast<PULONG>(&buffer_size));

    if ( !SUCCEEDED(hresult) ) {
        err << __FUNCTION__ ": GetSymbolPath failed" << endlerr;
        return false;
    }

    std::string check_path = symbol_path_buffer.get();

    if ( check_path.empty() || check_path == " " ) {
        if ( display_error ) {
            err << __FUNCTION__ << ": seems that your symbol path is empty. Fix it!" << endlerr;
        }
    } else if ( check_path.find(test_path) == std::string::npos ) {
        if ( display_error ) {
            warn << __FUNCTION__ << ": seems that your symbol path may be incorrect. ";
            warn << "Include symbol path (" << test_path << ")" << endlwarn;
        }
    } else {
        result = true;
    }

    return result;
}

void WDbgArk::WalkAnyListWithOffsetToRoutine(const std::string &list_head_name,
                                             const unsigned __int64 offset_list_head,
                                             const unsigned __int32 link_offset,
                                             const bool is_double,
                                             const unsigned __int32 offset_to_routine,
                                             const std::string &type,
                                             const std::string &ext_info,
                                             walkresType &output_list) {
    unsigned __int64 offset               = offset_list_head;
    unsigned __int64 list_head_offset_out = 0;

    if ( !offset_to_routine ) {
        err << __FUNCTION__ << ": invalid parameter" << endlerr;
        return;
    }

    if ( !offset ) {
        if ( !GetSymbolOffset(list_head_name.c_str(), true, &offset) ) {
            err << __FUNCTION__ << ": failed to get " << list_head_name << endlerr;
            return;
        } else {
            list_head_offset_out = offset;
        }
    }

    try {
        ExtRemoteList list_head(offset, link_offset, is_double);

        for ( list_head.StartHead(); list_head.HasNode(); list_head.Next() ) {
            unsigned __int64 node = list_head.GetNodeOffset();
            ExtRemoteData structure_data(node + offset_to_routine, m_PtrSize);

            unsigned __int64 routine = structure_data.GetPtr();

            if ( routine ) {
                OutputWalkInfo info;

                info.routine_address = routine;
                info.type = type;
                info.info = ext_info;
                info.list_head_name = list_head_name;
                info.object_offset = node;
                info.list_head_offset = list_head_offset_out;

                output_list.push_back(info);
            }
        }
    }
    catch ( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
    }
}

void WDbgArk::WalkAnyListWithOffsetToObjectPointer(const std::string &list_head_name,
                                                   const unsigned __int64 offset_list_head,
                                                   const bool is_double,
                                                   const unsigned __int32 offset_to_object_pointer,
                                                   void* context,
                                                   pfn_any_list_w_pobject_walk_callback_routine callback) {
    unsigned __int64 offset = offset_list_head;

    if ( !offset_to_object_pointer ) {
        err << __FUNCTION__ << ": invalid parameter offset_to_object_pointer" << endlerr;
        return;
    }

    if ( !offset && !GetSymbolOffset(list_head_name.c_str(), true, &offset) ) {
        err << __FUNCTION__ << ": failed to get " << list_head_name << endlerr;
        return;
    }

    try {
        ExtRemoteList list_head(offset, 0, is_double);

        for ( list_head.StartHead(); list_head.HasNode(); list_head.Next() ) {
            ExtRemoteData object_pointer(list_head.GetNodeOffset() + offset_to_object_pointer, m_PtrSize);

            if ( !SUCCEEDED(callback(this, object_pointer, context)) ) {
                err << __FUNCTION__ << ": error while invoking callback" << endlerr;
                return;
            }
        }
    }
    catch ( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
    }
}

void WDbgArk::WalkDirectoryObject(const unsigned __int64 directory_address,
                                  void* context,
                                  pfn_object_directory_walk_callback_routine callback) {
    if ( !directory_address ) {
        err << __FUNCTION__ << ": invalid directory address" << endlerr;
        return;
    }

    if ( !callback ) {
        err << __FUNCTION__ << ": invalid callback address" << endlerr;
        return;
    }

    try {
        ExtRemoteTyped directory_object("nt!_OBJECT_DIRECTORY", directory_address, false, NULL, NULL);
        ExtRemoteTyped buckets = directory_object.Field("HashBuckets");

        const unsigned __int32 num_buckets = buckets.GetTypeSize() / m_PtrSize;

        for ( __int64 i = 0; i < num_buckets; i++ ) {
            for ( ExtRemoteTyped directory_entry = *buckets[i];
                  directory_entry.m_Offset;
                  directory_entry = *directory_entry.Field("ChainLink") ) {
                ExtRemoteTyped object = *directory_entry.Field("Object");

                if ( !SUCCEEDED(callback(this, object, context)) ) {
                    err << __FUNCTION__ << ": error while invoking callback" << endlerr;
                    return;
                }
            }
        }
    }
    catch ( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
    }
}

void WDbgArk::WalkDeviceNode(const unsigned __int64 device_node_address,
                             void* context,
                             pfn_device_node_walk_callback_routine callback) {
    unsigned __int64 offset = device_node_address;

    if ( !callback ) {
        err << __FUNCTION__ << ": invalid callback address" << endlerr;
        return;
    }

    try {
        if ( !offset ) {
            if ( !GetSymbolOffset("nt!IopRootDeviceNode", true, &offset) ) {
                err << __FUNCTION__ << ": failed to get nt!IopRootDeviceNode" << endlerr;
                return;
            } else {
                ExtRemoteData device_node_ptr(offset, m_PtrSize);
                offset = device_node_ptr.GetPtr();
            }
        }

        ExtRemoteTyped device_node("nt!_DEVICE_NODE", offset, false, NULL, NULL);

        for ( ExtRemoteTyped child_node = *device_node.Field("Child");
              child_node.m_Offset;
              child_node = *child_node.Field("Sibling") ) {
            if ( !SUCCEEDED(callback(this, child_node, context)) ) {
                err << __FUNCTION__ << ": error while invoking callback" << endlerr;
                return;
            }

            WalkDeviceNode(child_node.m_Offset, context, callback);
        }
    }
    catch ( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
    }
}

void WDbgArk::AddSymbolPointer(const std::string &symbol_name,
                               const std::string &type,
                               const std::string &additional_info,
                               walkresType &output_list) {
    unsigned __int64 offset = 0;

    try {
        if ( GetSymbolOffset(symbol_name.c_str(), true, &offset) ) {
            unsigned __int64 symbol_offset = offset;

            ExtRemoteData routine_ptr(offset, m_PtrSize);
            offset = routine_ptr.GetPtr();

            if ( offset ) {
                OutputWalkInfo info;

                info.routine_address = offset;
                info.type = type;
                info.info = additional_info;
                info.list_head_name = symbol_name;
                info.object_offset = 0ULL;
                info.list_head_offset = symbol_offset;

                output_list.push_back(info);
            }
        }
    }
    catch ( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
    }
}

// TODO(swwwolf): get human disassembler, not this piece of shit with strings
bool WDbgArk::FindDbgkLkmdCallbackArray() {
    #define MAX_INSN_LENGTH 15

    unsigned __int64 offset = 0;
    bool             result = false;

    if ( m_minor_build < W7RTM_VER ) {
        out << __FUNCTION__ << ": unsupported Windows version" << endlout;
        return false;
    }

    if ( GetSymbolOffset("nt!DbgkLkmdCallbackArray", true, &offset) )
        return true;

    if ( !GetSymbolOffset("nt!DbgkLkmdUnregisterCallback", true, &offset) ) {
        err << __FUNCTION__ << ": can't find nt!DbgkLkmdUnregisterCallback" << endlerr;
        return false;
    }

    try {
        ExtRemoteData test_offset(offset, m_PtrSize);
    }
    catch( const ExtRemoteException &Ex ) {
        err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
        return false;
    }

    unsigned __int64 cur_pointer = offset;
    unsigned __int64 end         = cur_pointer + MAX_INSN_LENGTH * 20;

    std::unique_ptr<char[]> disasm_buf(new char[0x100]);

    unsigned __int32 asm_options;

    if ( !SUCCEEDED(m_Control3->GetAssemblyOptions(reinterpret_cast<PULONG>(&asm_options))) )
        warn << __FUNCTION__ << ": failed to get assembly options" << endlwarn;

    if ( !SUCCEEDED(m_Control3->SetAssemblyOptions(DEBUG_ASMOPT_NO_CODE_BYTES)) )
        warn << __FUNCTION__ << ": failed to set assembly options" << endlwarn;

    while ( cur_pointer < end ) {
        HRESULT disasm_result = m_Control->Disassemble(cur_pointer,
                                                       0,
                                                       disasm_buf.get(),
                                                       0x100,
                                                       nullptr,
                                                       &cur_pointer);

        if ( !SUCCEEDED(disasm_result) ) {
            err << __FUNCTION__ " : disassembly failed at " << std::hex << std::showbase << cur_pointer << endlerr;
            break;
        }

        std::string disasm   = disasm_buf.get();
        size_t      posstart = 0;
        size_t      posend   = 0;
        size_t      pos      = 0;

        // TODO(swwwolf): regexp?
        if ( m_is_cur_machine64 ) {
            pos = disasm.find("lea", 0);

            if ( pos == std::string::npos )
                continue;

            pos = disasm.find(",[", pos);

            if ( pos == std::string::npos )
                continue;

            posstart = disasm.find("(", pos);

            if ( posstart == std::string::npos )
                continue;

            posend = disasm.find(")", posstart);

            if ( posstart == std::string::npos )
                continue;
        } else {
            pos = disasm.find("mov", 0);

            if ( pos == std::string::npos )
                continue;

            pos = disasm.find(",offset", pos);

            if ( pos == std::string::npos )
                continue;

            posstart = disasm.find("(", pos);

            if ( posstart == std::string::npos )
                continue;

            posend = disasm.find(")", posstart);

            if ( posstart == std::string::npos )
                continue;
        }

        std::string string_value(disasm.substr(posstart + 1, posend - posstart - 1));

        try {
            unsigned __int64 ret_address = g_Ext->EvalExprU64(string_value.c_str());

            // do not reload nt module after that
            DEBUG_MODULE_AND_ID id;

            HRESULT hresult = m_Symbols3->AddSyntheticSymbol(ret_address,
                                                             m_PtrSize,
                                                             "DbgkLkmdCallbackArray",
                                                             DEBUG_ADDSYNTHSYM_DEFAULT,
                                                             &id);

            if ( !SUCCEEDED(hresult) ) {
                err << __FUNCTION__ << ": failed to add synthetic symbol DbgkLkmdCallbackArray" << endlerr;
            } else {
                m_synthetic_symbols.push_back(id);
                result = true;
            }
        }
        catch ( const ExtStatusException &Ex ) {
            err << __FUNCTION__ << ": " << Ex.GetMessage() << endlerr;
        }

        break;
    }

    if ( !SUCCEEDED(m_Control3->SetAssemblyOptions(asm_options)) )
        warn << __FUNCTION__ << ": failed to set assembly options" << endlwarn;

    return result;
}

void WDbgArk::RemoveSyntheticSymbols(void) {
    for ( DEBUG_MODULE_AND_ID id : m_synthetic_symbols ) {
        if ( !SUCCEEDED(g_Ext->m_Symbols3->RemoveSyntheticSymbol(&id)) ) {
            warn << __FUNCTION__ << ": failed to remove synthetic symbol ";
            warn << std::hex << std::showbase << id.Id << endlwarn;
        }
    }
}

// don't include resource.h
#define IDR_RT_RCDATA1 105
#define IDR_RT_RCDATA2 106
bool WDbgArk::InitDummyPdbModule(void) {
    char* resource_name = nullptr;
    ExtCaptureOutputA ignore_output;  // destructor will call Destroy and Stop

    ignore_output.Start();

    if ( !RemoveDummyPdbModule() ) {
        err << __FUNCTION__ << ": RemoveDummyPdbModule failed" << endlerr;
        return false;
    }

    if ( m_is_cur_machine64 )
        resource_name = MAKEINTRESOURCE(IDR_RT_RCDATA2);
    else
        resource_name = MAKEINTRESOURCE(IDR_RT_RCDATA1);

    std::unique_ptr<WDbgArkResHelper> res_helper(new WDbgArkResHelper);

    if ( !res_helper->DropResource(resource_name, "RT_RCDATA", "dummypdb.pdb") ) {
        err << __FUNCTION__ << ": DropResource failed" << endlerr;
        return false;
    }

    std::string drop_path = res_helper->GetDropPath();

    if ( !CheckSymbolsPath(drop_path, false) ) {
        if ( !SUCCEEDED(m_Symbols->AppendSymbolPath(drop_path.c_str())) ) {
            err << __FUNCTION__ << ": AppendSymbolPath failed" << endlerr;
            return false;
        }
    }

    if ( !SUCCEEDED(m_Symbols->Reload("/i dummypdb=0xFFFFFFFFFFFFF000,0xFFF")) ) {
        err << __FUNCTION__ << ": Reload failed" << endlerr;
        return false;
    }

    return true;
}

bool WDbgArk::RemoveDummyPdbModule(void) {
    if ( SUCCEEDED(m_Symbols->GetModuleByModuleName("dummypdb", 0, nullptr, nullptr)) ) {
        if ( !SUCCEEDED(m_Symbols->Reload("/u dummypdb")) ) {
            err << __FUNCTION__ << ": Failed to unload dummypdb module" << endlerr;
            return false;
        }
    }

    return true;
}
