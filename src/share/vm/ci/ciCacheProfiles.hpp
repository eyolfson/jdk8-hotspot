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

#ifndef SHARE_VM_CI_CICACHEPROFILES_HPP
#define SHARE_VM_CI_CICACHEPROFILES_HPP

#include "ci/ciMethod.hpp"

// -------------------
// ciCacheProfiles & ciCacheProfilesBroker
// -------------------
//
// Cache profiling information of a java method to disk and retrieve this data
// in further executions of the JVM.
//
// NOTE: this functionality is only enabled in "experimental mode"
// (flag: -XX:+UnlockExperimentalVMOptions)
//
// -------------------
// Dump profile data.
// -------------------
//
// Use the flag -XX:+DumpProfiles to dump all compiled methods to disk (cached_profiles.dat)
//
// One can also specify the flags DumpProfile or IgnoreDumpProfile as CompileCommand
// for specific method inclusions / exclusions
// e.g. -XX:CompileCommand=option,Benchmark::test,DumpProfile
//
// -------------------
// Use profile data.
// -------------------
//
// The flag -XX:+CacheProfiles tells the VM to use cached profiles when available
// Before any compilation happen this class scans through the txt file and builds
// the datastructure (consider the linear overhead).
// One can also specify a profiles file with -XX:CacheProfilesFile=foo.txt
//
// CacheProfiles can be used in 3 different modes:
// with flag -XX:CacheProfilesMode={0,1,2}
//
// Mode 0: lower the compilation threshold scaling of cached methods automatically
//         to 0.01 of cached methods automatically so they get compiled
//         earlier and with the highest available profile (usually C2)
//         One can use a different value than 0.01 by using the flag
//         -XX:CacheProfilesMode0ThresholdScaling=x.xx
// Mode 1: do not lower the thresholds but once a compilation is triggered
//         use highest available profile (usually C2)
// Mode 2: do not lower the thresholds but use compile level 2 (limited profiles)
//         instead of compile level 3 (full profiles) when a cached method gets
//         compiled with C1.
//         The idea is that this method already has enough profiles (on disk)
//         available so we get a faster C1 phase.
//         C2 will still use the cached profile like in other modes.
//
// -------------------
// Debug flags.
// -------------------
//
// -XX:+PrintCacheProfiles - prints debug information about cache profiles thread
// -XX:+PrintDeoptimizationCount - prints deoptimization count after JVM execution
// -XX:+PrintDeoptimizationCountVerbose - prints deopt count after each compilation
// -XX:+PrintCompileQueueSize - print size of compile queue after each compilation

// class to represent the cached compilations
class MethodRecord : public CHeapObj<mtCompiler> {
public:
  char* _klass_name;
  char* _method_name;
  char* _signature;

  int _instructions_size;
  int _interpreter_invocation_count;
  int _interpreter_throwout_count;
  int _invocation_counter;
  int _backedge_counter;
  void setupMethodRecord(char* k_name, char* m_name, char* sig) {
    _klass_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(k_name)+1, mtCompiler);;
    _method_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(m_name)+1, mtCompiler);;
    _signature = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(sig)+1, mtCompiler);;
    strcpy(_klass_name,k_name);
    strcpy(_method_name,m_name);
    strcpy(_signature,sig);
  }
};

class MethodDataRecord : public CHeapObj<mtCompiler> {
public:
  char* _klass_name;
  char* _method_name;
  char* _signature;

  int _state;
  int _current_mileage;

  intptr_t* _data;
  char*     _orig_data;
  Klass**   _classes;
  Method**  _methods;
  int*      _classes_offsets;
  int*      _methods_offsets;
  int       _data_length;
  int       _orig_data_length;
  int       _classes_length;
  int       _methods_length;
  void setupMethodDataRecord(char* k_name, char* m_name, char* sig) {
    _klass_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(k_name)+1, mtCompiler);;
    _method_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(m_name)+1, mtCompiler);;
    _signature = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(sig)+1, mtCompiler);;
    strcpy(_klass_name,k_name);
    strcpy(_method_name,m_name);
    strcpy(_signature,sig);
  }
};

class InlineRecord : public CHeapObj<mtCompiler> {
public:
  char* _klass_name;
  char* _method_name;
  char* _signature;

  int _inline_depth;
  int _inline_bci;
  void setupInlineRecord(char* k_name, char* m_name, char* sig) {
    _klass_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(k_name)+1, mtCompiler);;
    _method_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(m_name)+1, mtCompiler);;
    _signature = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(sig)+1, mtCompiler);;
    strcpy(_klass_name,k_name);
    strcpy(_method_name,m_name);
    strcpy(_signature,sig);
  }
};

class CompileRecord : public CHeapObj<mtCompiler> {
public:

  // "compile" data
  ciKlass* _iklass;
  Method*  _imethod;

  MethodRecord**     _method_records;
  MethodDataRecord** _method_data_records;
  int _method_records_pos;
  int _method_data_records_pos;
  int _method_records_length;
  int _method_data_records_length;

  char* _klass_name;
  char* _method_name;
  char* _signature;
  int   _entry_bci;
  int   _comp_level;
  GrowableArray<InlineRecord*>* _inline_records;

  void setupCompileRecord(char* k_name, char* m_name, char* sig) {
    _klass_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(k_name)+1, mtCompiler);;
    _method_name = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(m_name)+1, mtCompiler);;
    _signature = NEW_C_HEAP_ARRAY(char, (unsigned)strlen(sig)+1, mtCompiler);;
    strcpy(_klass_name,k_name);
    strcpy(_method_name,m_name);
    strcpy(_signature,sig);
    _inline_records = NULL;
  }
 //  Create and initialize a record for a ciInlineRecord
 InlineRecord* new_inlineRecord(Method* method, int bci, int depth) {
  InlineRecord* rec = NEW_RESOURCE_OBJ(InlineRecord);
  rec->_klass_name = method->method_holder()->name()->as_utf8();
  rec->_method_name = method->name()->as_utf8();
  rec->_signature = method->signature()->as_utf8();
  rec->_inline_bci = bci;
  rec->_inline_depth = depth;
  _inline_records->append(rec);
  return rec;
  }

  // Lookup inlining data for a ciMethod
 InlineRecord* find_inlineRecord(Method* method, int bci, int depth) {
  if (_inline_records != NULL) {
    return find_inlineRecord(_inline_records, method, bci, depth);
  }
  return NULL;
 }

 InlineRecord* find_inlineRecord(GrowableArray<InlineRecord*>* records,
  Method* method, int bci, int depth) {
  if (records != NULL) {
    const char* klass_name = method->method_holder()->name()->as_utf8();
    const char* method_name = method->name()->as_utf8();
    const char* signature = method->signature()->as_utf8();
    for (int i = 0; i < records->length(); i++) {
      InlineRecord* rec = records->at(i);
      if ((rec->_inline_bci == bci) &&
        (rec->_inline_depth == depth) &&
        (strcmp(rec->_klass_name, klass_name) == 0) &&
        (strcmp(rec->_method_name, method_name) == 0) &&
        (strcmp(rec->_signature, signature) == 0)) {
        return rec;
      }
    }
  }
  return NULL;
 }
};

class ciCacheProfiles : AllStatic {
  CI_PACKAGE_ACCESS
  friend class ciCacheProfilesBroker;

//#ifndef PRODUCT
 private:
  static FILE*   _stream;
  static Thread* _thread;
  static Handle  _protection_domain;
  static Handle  _loader;

  static const char* _error_message;

  static char* _bufptr;
  static char* _buffer;
  static int   _buffer_length;
  static int   _buffer_pos;

  // "compile" data
  static ciKlass* _iklass;
  static Method*  _imethod;

  static int replay_impl(TRAPS, Method* method, int osr_bci);

  static CompileRecord**    _compile_records;
  static MethodRecord** _method_records;
  static MethodDataRecord** _method_data_records;

  static int _method_records_pos;
  static int _method_data_records_pos;
  static int _compile_records_pos;
  static int _method_records_length;
  static int _method_data_records_length;
  static int _compile_records_length;

  static bool _had_loading_error;
  static bool CacheIgnoreInitErrors;
  static bool _initialized;

  static bool had_error();
  static bool can_replay();
  static void report_error(const char* msg);
  static int parse_int(const char* label);
  static intptr_t parse_intptr_t(const char* label);
  static void skip_ws();
  static char* scan_and_terminate(char delim);
  static char* parse_string();
  static char* parse_quoted_string();
  static const char* parse_escaped_string();
  // Look for the tag 'tag' followed by an
  static bool parse_tag_and_count(const char* tag, int& length) ;
  // Parse a sequence of raw data encoded as bytes and return the
  // resulting data.
  static char* parse_data(const char* tag, int& length);
  // Parse a standard chunk of data emitted as:
  //   'tag' <length> # # ...
  // Where each # is an intptr_t item
  static intptr_t* parse_intptr_data(const char* tag, int& length);
  // Parse a possibly quoted version of a symbol into a symbolOop
  static Symbol* parse_symbol(TRAPS);
  // Parse a valid klass name and look it up
  static Klass* parse_klass(TRAPS);
  // Lookup a klass
  static Klass* resolve_klass(const char* klass, TRAPS);
  // Parse the standard tuple of <klass> <name> <signature>
  static Method* parse_method(TRAPS);
  static int get_line(int c);

  static void unescape_string(char* value);

  static const char* error_message();

  static void process(TRAPS);
  static void process_command(TRAPS);

  static void process_compile(TRAPS);
  static void process_ciMethod(TRAPS);
  static void process_ciMethodData(TRAPS);
  static void process_JvmtiExport(TRAPS);

  // Create and initialize a record for a ciMethod
  static MethodRecord* new_methodRecord(char* klass_name, char* method_name, char* signature);

  // Create and initialize a record for a ciMethodData
  static MethodDataRecord* new_methodDataRecord(char* klass_name, char* method_name, char* signature);

  // Create and initialize a record for a ciCompile
  // returns NULL if we should keep existing record
  static CompileRecord* new_compileRecord(char* klass_name, char* method_name, char* signature, int comp_level);

  // Lookup data for a ciMethod
  static CompileRecord* find_compileRecord(Method* method);
  // Lookup data for a methodHandle
  static CompileRecord* find_compileRecord(methodHandle method);
  // Lookup data for a set of strings
  static CompileRecord* find_compileRecord(char* klass_name, char* method_name, char* signature);


 public:
  // parse cached profiles to set flag in methods
  static void initialize(TRAPS);

  static int is_cached(Method* method);
  static int is_cached(methodHandle method);

  static bool is_initialized();
  static void is_initialized(bool flag);
  //static void reset();

//#endif
};

#endif // SHARE_VM_CI_CICACHEPROFILES_HPP
