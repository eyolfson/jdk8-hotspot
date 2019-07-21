/* Copyright (c) 2015, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "ci/ciMethodData.hpp"
#include "ci/ciCacheProfilesBroker.hpp"
#include "ci/ciSymbol.hpp"
#include "ci/ciKlass.hpp"
#include "ci/ciUtilities.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/compilerOracle.hpp"
#include "memory/allocation.inline.hpp"
#include "memory/oopFactory.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "utilities/copy.hpp"
#include "utilities/macros.hpp"


Thread* ciCacheProfilesBroker::_thread = NULL;

const char* ciCacheProfilesBroker::_error_message = NULL;

// "compile" data
ciKlass* ciCacheProfilesBroker::_iklass = NULL;
Method*  ciCacheProfilesBroker::_imethod = NULL;

MethodRecord**    ciCacheProfilesBroker::_method_records = NULL;
MethodDataRecord**    ciCacheProfilesBroker::_method_data_records = NULL;

int ciCacheProfilesBroker::_method_records_pos = 0;
int ciCacheProfilesBroker::_method_records_length = 0;
int ciCacheProfilesBroker::_method_data_records_pos = 0;
int ciCacheProfilesBroker::_method_data_records_length = 0;

bool ciCacheProfilesBroker::had_error() {
  return _error_message != NULL || _thread->has_pending_exception();
}

bool ciCacheProfilesBroker::can_replay() {
  return !(!ciCacheProfiles::is_initialized() || had_error());
}
const char* ciCacheProfilesBroker::error_message() {
  return _error_message;
}

void ciCacheProfilesBroker::replay(TRAPS, Method* method, int osr_bci) {
  _thread = THREAD;
  int exit_code = replay_impl(THREAD, method, osr_bci);
}

int ciCacheProfilesBroker::replay_impl(TRAPS, Method* method, int osr_bci) {
  HandleMark hm;

  int exit_code = 0;
  if (can_replay()) {
    replay_method(THREAD, method, osr_bci);
  } else {
    exit_code = 1;
    return exit_code;
  }

  if (HAS_PENDING_EXCEPTION) {
    oop throwable = PENDING_EXCEPTION;
    CLEAR_PENDING_EXCEPTION;
    java_lang_Throwable::print(throwable, tty);
    tty->cr();
    java_lang_Throwable::print_stack_trace(throwable, tty);
    tty->cr();
    exit_code = 2;
  }

  if (had_error()) {
    tty->print_cr("Failed on %s", error_message());
    exit_code = 1;
  }
  return exit_code;
}

// validation of comp_level
bool ciCacheProfilesBroker::is_valid_comp_level(int comp_level) {
  const int msg_len = 256;
  char* msg = NULL;
  if (!is_compile(comp_level)) {
    msg = NEW_RESOURCE_ARRAY(char, msg_len);
    jio_snprintf(msg, msg_len, "%d isn't compilation level", comp_level);
  } else if (!TieredCompilation && (comp_level != CompLevel_highest_tier)) {
    msg = NEW_RESOURCE_ARRAY(char, msg_len);
    switch (comp_level) {
      case CompLevel_simple:
        jio_snprintf(msg, msg_len, "compilation level %d requires Client VM or TieredCompilation", comp_level);
        break;
      case CompLevel_full_optimization:
        jio_snprintf(msg, msg_len, "compilation level %d requires Server VM", comp_level);
        break;
      default:
        jio_snprintf(msg, msg_len, "compilation level %d requires TieredCompilation", comp_level);
    }
  }
  if (msg != NULL) {
    _error_message = msg;
    return false;
  }
  return true;
}

void ciCacheProfilesBroker::replay_method(TRAPS, Method* method, int osr_bci) {
  ResourceMark rm;
  CompileRecord* rec = ciCacheProfiles::find_compileRecord(method);
  if(rec!=NULL) {
    _method_data_records = rec->_method_data_records;
    _method_records = rec->_method_records;

     _method_data_records_pos = rec->_method_data_records_pos;
     _method_records_pos = rec->_method_data_records_pos;
     _method_data_records_length = rec->_method_data_records_length;
     _method_records_length = rec->_method_data_records_length;

    _iklass = rec->_iklass;
    _imethod = rec->_imethod;

    // old version w/o comp_level
    if (!is_valid_comp_level(rec->_comp_level)) {
      return;
    }
    Klass* k = method->method_holder();
    ((InstanceKlass*)k)->initialize(THREAD);
    if (HAS_PENDING_EXCEPTION) {
      oop throwable = PENDING_EXCEPTION;
      java_lang_Throwable::print(throwable, tty);
      tty->cr();
      if (ciCacheProfiles::CacheIgnoreInitErrors) {
        CLEAR_PENDING_EXCEPTION;
        ((InstanceKlass*)k)->set_init_state(InstanceKlass::fully_initialized);
      } else {
        return;
      }
    }
    // Make sure the existence of a prior compile doesn't stop this one
    nmethod* nm = (rec->_entry_bci != InvocationEntryBci) ? method->lookup_osr_nmethod_for(osr_bci, rec->_comp_level, true) : method->code();
    if (nm != NULL) {
      nm->make_not_entrant();
    }
    CompileBroker::compile_method_base(method, osr_bci, rec->_comp_level,
                                  methodHandle(), 0, "replay", THREAD);

    //reset();
  }
  else {
    if(PrintCacheProfiles) tty->print("ERROR IN COMPILE");
  }
}

void ciCacheProfilesBroker::initialize(ciMethodData* m) {
  if (!ciCacheProfiles::is_initialized()) {
    return;
  }

  ASSERT_IN_VM;
  ResourceMark rm;

  Method* method = m->get_MethodData()->method();
  MethodDataRecord* rec = find_methodDataRecord(method);
  if (rec == NULL) {
    // This indicates some mismatch with the original environment and
    // the replay environment though it's not always enough to
    // interfere with reproducing a bug
    // TODO is this still an issue in that case?
    //tty->print_cr("Warning: requesting ciMethodData record for method with no data: ");
    //method->print_name(tty);
    //tty->cr();
  } else {
    m->_state = rec->_state;
    m->_current_mileage = rec->_current_mileage;
    if (rec->_data_length != 0) {
      bool check1 = m->_data_size + m->_extra_data_size == rec->_data_length * (int)sizeof(rec->_data[0]);
      bool check2 = m->_data_size == rec->_data_length * (int)sizeof(rec->_data[0]);
      //assert(check1 || check2, "must agree");
      // TODO: might require additional thoughts, is this always a correct way to do it?
      if(check1 || check2) {
        // Write the correct ciObjects back into the profile data
        ciEnv* env = ciEnv::current();
        for (int i = 0; i < rec->_classes_length; i++) {
          Klass *k = rec->_classes[i];
          // In case this class pointer is is tagged, preserve the tag
          // bits
          rec->_data[rec->_classes_offsets[i]] =
            ciTypeEntries::with_status(env->get_metadata(k)->as_klass(), rec->_data[rec->_classes_offsets[i]]);
        }
        for (int i = 0; i < rec->_methods_length; i++) {
          Method *m = rec->_methods[i];
          *(ciMetadata**)(rec->_data + rec->_methods_offsets[i]) =
            env->get_metadata(m);
        }
        // Copy the updated profile data into place as intptr_ts
  #ifdef _LP64
        Copy::conjoint_jlongs_atomic((jlong *)rec->_data, (jlong *)m->_data, rec->_data_length);
  #else
        Copy::conjoint_jints_atomic((jint *)rec->_data, (jint *)m->_data, rec->_data_length);
  #endif
      } else {
        if(PrintCacheProfiles) tty->print("ERROR on retrieving compile data for %s::%s: sizes do not match\n", rec->_klass_name, rec->_method_name);
      }

      // copy in the original header
      Copy::conjoint_jbytes(rec->_orig_data, (char*)&m->_orig, rec->_orig_data_length);
    }
  }
}

void ciCacheProfilesBroker::initialize(ciMethod* m) {
  if (!ciCacheProfiles::is_initialized()) {
    return;
  }

  ASSERT_IN_VM;
  ResourceMark rm;

  Method* method = m->get_Method();
  MethodRecord* rec = find_methodRecord(method);
  if (rec == NULL) {
    // This indicates some mismatch with the original environment and
    // the replay environment though it's not always enough to
    // interfere with reproducing a bug
    // TODO is this still an issue in that case?
    //tty->print_cr("Warning: requesting ciMethod record for method with no data: ");
    //method->print_name(tty);
    //tty->cr();
  } else {
    EXCEPTION_CONTEXT;
    // m->_instructions_size = rec->_instructions_size;
    m->_instructions_size = -1;
    m->_interpreter_invocation_count = rec->_interpreter_invocation_count;
    m->_interpreter_throwout_count = rec->_interpreter_throwout_count;
    MethodCounters* mcs = method->get_method_counters(CHECK_AND_CLEAR);
    guarantee(mcs != NULL, "method counters allocation failed");
    mcs->invocation_counter()->_counter = rec->_invocation_counter;
    mcs->backedge_counter()->_counter = rec->_backedge_counter;
  }
}

// Lookup data for a ciMethod
MethodRecord* ciCacheProfilesBroker::find_methodRecord(Method* method) {
  const char* klass_name =  method->method_holder()->name()->as_utf8();
  const char* method_name = method->name()->as_utf8();
  const char* signature = method->signature()->as_utf8();
  for (int i = 0; i < _method_records_pos; i++) {
    MethodRecord* rec = _method_records[i];
    if (strcmp(rec->_klass_name, klass_name) == 0 &&
        strcmp(rec->_method_name, method_name) == 0 &&
        strcmp(rec->_signature, signature) == 0) {
      return rec;
    }
  }
  return NULL;
}

// Lookup data for a ciMethodData
MethodDataRecord* ciCacheProfilesBroker::find_methodDataRecord(Method* method) {
  char* klass_name =  method->method_holder()->name()->as_utf8();
  char* method_name = method->name()->as_utf8();
  char* signature = method->signature()->as_utf8();
  for (int i = 0; i < _method_data_records_pos; i++) {
    MethodDataRecord* rec = _method_data_records[i];
    if (strcmp(rec->_klass_name, klass_name) == 0 &&
        strcmp(rec->_method_name, method_name) == 0 &&
        strcmp(rec->_signature, signature) == 0) {
      return rec;
    }
  }
  return NULL;
}
