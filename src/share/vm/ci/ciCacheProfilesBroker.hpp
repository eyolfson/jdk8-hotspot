/*
 * Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

// This class contains the functionality of CacheProfiles that interact
// with the CompileBroker when cached profiles are loaded.
// more information about this functionality can be found in
// ciCacheProfiles.hpp

#ifndef SHARE_VM_CI_CICACHEPROFILESBROKER_HPP
#define SHARE_VM_CI_CICACHEPROFILESBROKER_HPP

#include "ci/ciMethod.hpp"
#include "ci/ciCacheProfiles.hpp"

class ciCacheProfilesBroker : public AllStatic {
  CI_PACKAGE_ACCESS
  friend class ciCacheProfiles;
private:
  static Thread* _thread;
  static const char* _error_message;

  static ciKlass* _iklass;
  static Method*  _imethod;

  static MethodRecord** _method_records;
  static MethodDataRecord** _method_data_records;
  static int _method_records_pos;
  static int _method_data_records_pos;

  static int _method_records_length;
  static int _method_data_records_length;

  static bool had_error();
  static bool can_replay();
  // validation of comp_level
  static bool is_valid_comp_level(int comp_level);

  static const char* error_message();

  static int replay_impl(TRAPS, Method* method, int osr_bci);
  static void replay_method(TRAPS, Method* method, int osr_bci);

  // Lookup data for a ciMethod
  static MethodRecord* find_methodRecord(Method* method);
  // Lookup data for a ciMethodData
  static MethodDataRecord* find_methodDataRecord(Method* method);

public:
  // Replay specified compilation
  static void replay(TRAPS, Method* method, int osr_bci);
  // These are used by the CI to fill in the cached data from the
  // replay file when replaying compiles.
  static void initialize(ciMethodData* method);
  static void initialize(ciMethod* method);

};

#endif // SHARE_VM_CI_CICACHEPROFILES_HPP
