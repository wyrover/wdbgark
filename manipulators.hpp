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

//////////////////////////////////////////////////////////////////////////
//  Include this after "#define EXT_CLASS WDbgArk" only
//////////////////////////////////////////////////////////////////////////

#if _MSC_VER > 1000
#pragma once
#endif

#ifndef MANIPULATORS_HPP_
#define MANIPULATORS_HPP_

#include <sstream>
#include <iomanip>

#include <engextcpp.hpp>

/* global stream manipulators */
inline std::ostream& endlout(std::ostream& arg) {
    std::stringstream ss;

    arg << "\n";
    ss << "[+] " << arg.rdbuf();
    g_Ext->Dml("%s", ss.str().c_str());
    return arg.flush();
}

inline std::ostream& endlwarn(std::ostream& arg) {
    std::stringstream ss;

    arg << "\n";
    ss << "[?] " << arg.rdbuf();
    g_Ext->DmlWarn("%s", ss.str().c_str());
    return arg.flush();
}

inline std::ostream& endlerr(std::ostream& arg) {
    std::stringstream ss;

    arg << "\n";
    ss << "[-] " << arg.rdbuf();
    g_Ext->DmlErr("%s", ss.str().c_str());
    return arg.flush();
}

#endif  // MANIPULATORS_HPP_
