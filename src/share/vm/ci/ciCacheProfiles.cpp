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
#include "ci/ciCacheProfiles.hpp"
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

//#ifndef PRODUCT

// ciCacheProfiles

FILE*   ciCacheProfiles::_stream = NULL;
Thread* ciCacheProfiles::_thread = NULL;
Handle  ciCacheProfiles::_protection_domain;
Handle  ciCacheProfiles::_loader;

const char* ciCacheProfiles::_error_message;

char* ciCacheProfiles::_bufptr = NULL;
char* ciCacheProfiles::_buffer = NULL;
int   ciCacheProfiles::_buffer_length = 0;
int   ciCacheProfiles::_buffer_pos = 0;

// "compile" data
ciKlass* ciCacheProfiles::_iklass = NULL;
Method*  ciCacheProfiles::_imethod = NULL;

// records that store profiling information
CompileRecord**    ciCacheProfiles::_compile_records = NULL;
MethodRecord**    ciCacheProfiles::_method_records = NULL;
MethodDataRecord**    ciCacheProfiles::_method_data_records = NULL;

int ciCacheProfiles::_method_records_pos = 0;
int ciCacheProfiles::_method_records_length = 0;
int ciCacheProfiles::_method_data_records_pos = 0;
int ciCacheProfiles::_method_data_records_length = 0;
int ciCacheProfiles::_compile_records_pos = 0;
int ciCacheProfiles::_compile_records_length = 0;

bool ciCacheProfiles::_had_loading_error = false;
bool ciCacheProfiles::CacheIgnoreInitErrors = true;
bool ciCacheProfiles::_initialized = false;

bool ciCacheProfiles::had_error() {
  return _error_message != NULL || _thread->has_pending_exception();
}

bool ciCacheProfiles::can_replay() {
  return !(_stream == NULL || had_error());
}

void ciCacheProfiles::report_error(const char* msg) {
  _error_message = msg;
  // Restore the _buffer contents for error reporting
  for (int i = 0; i < _buffer_pos; i++) {
    if (_buffer[i] == '\0') _buffer[i] = ' ';
  }
}

int ciCacheProfiles::parse_int(const char* label) {
  if (had_error()) {
    return 0;
  }

  int v = 0;
  int read;
  if (sscanf(_bufptr, "%i%n", &v, &read) != 1) {
    report_error(label);
  } else {
    _bufptr += read;
  }
  return v;
}

intptr_t ciCacheProfiles::parse_intptr_t(const char* label) {
  if (had_error()) {
    return 0;
  }

  intptr_t v = 0;
  int read;
  if (sscanf(_bufptr, INTPTR_FORMAT "%n", &v, &read) != 1) {
    report_error(label);
  } else {
    _bufptr += read;
  }
  return v;
}

void ciCacheProfiles::skip_ws() {
  // Skip any leading whitespace
  while (*_bufptr == ' ' || *_bufptr == '\t') {
    _bufptr++;
  }
}


char* ciCacheProfiles::scan_and_terminate(char delim) {
  char* str = _bufptr;
  while (*_bufptr != delim && *_bufptr != '\0') {
    _bufptr++;
  }
  if (*_bufptr != '\0') {
    *_bufptr++ = '\0';
  }
  if (_bufptr == str) {
    // nothing here
    return NULL;
  }
  return str;
}

char* ciCacheProfiles::parse_string() {
  if (had_error()) return NULL;

  skip_ws();
  return scan_and_terminate(' ');
}

char* ciCacheProfiles::parse_quoted_string() {
  if (had_error()) return NULL;

  skip_ws();

  if (*_bufptr == '"') {
    _bufptr++;
    return scan_and_terminate('"');
  } else {
    return scan_and_terminate(' ');
  }
}

const char* ciCacheProfiles::parse_escaped_string() {
  char* result = parse_quoted_string();
  if (result != NULL) {
    unescape_string(result);
  }
  return result;
}

// Look for the tag 'tag' followed by an
bool ciCacheProfiles::parse_tag_and_count(const char* tag, int& length) {
  const char* t = parse_string();
  if (t == NULL) {
    return false;
  }

  if (strcmp(tag, t) != 0) {
    report_error(tag);
    return false;
  }
  length = parse_int("parse_tag_and_count");
  return !had_error();
}

// Parse a sequence of raw data encoded as bytes and return the
// resulting data.
char* ciCacheProfiles::parse_data(const char* tag, int& length) {
  if (!parse_tag_and_count(tag, length)) {
    return NULL;
  }

  char * result = NEW_C_HEAP_ARRAY(char, length, mtCompiler);
  for (int i = 0; i < length; i++) {
    int val = parse_int("data");
    result[i] = val;
  }
  return result;
}

// Parse a standard chunk of data emitted as:
//   'tag' <length> # # ...
// Where each # is an intptr_t item
intptr_t* ciCacheProfiles::parse_intptr_data(const char* tag, int& length) {
  if (!parse_tag_and_count(tag, length)) {
    return NULL;
  }

  intptr_t* result = NEW_C_HEAP_ARRAY(intptr_t, length, mtCompiler);
  for (int i = 0; i < length; i++) {
    skip_ws();
    intptr_t val = parse_intptr_t("data");
    result[i] = val;
  }
  return result;
}

// Parse a possibly quoted version of a symbol into a symbolOop
Symbol* ciCacheProfiles::parse_symbol(TRAPS) {
  const char* str = parse_escaped_string();
  if (str != NULL) {
    Symbol* sym = SymbolTable::lookup(str, (int)strlen(str), CHECK_NULL);
    return sym;
  }
  return NULL;
}

// Parse a valid klass name and look it up
Klass* ciCacheProfiles::parse_klass(TRAPS) {
  const char* str = parse_escaped_string();
  Symbol* klass_name = SymbolTable::lookup(str, (int)strlen(str), CHECK_NULL);
  if (klass_name != NULL) {
    Klass* k = NULL;
    if (_iklass != NULL) {
      k = (Klass*)_iklass->find_klass(ciSymbol::make(klass_name->as_C_string()))->constant_encoding();
    } else {
      k = SystemDictionary::resolve_or_fail(klass_name, _loader, _protection_domain, true, THREAD);
    }
    if (HAS_PENDING_EXCEPTION) {
      oop throwable = PENDING_EXCEPTION;
      java_lang_Throwable::print(throwable, tty);
      tty->cr();
      report_error(str);
      return NULL;
    }
    return k;
  }
  return NULL;
}

// Lookup a klass
Klass* ciCacheProfiles::resolve_klass(const char* klass, TRAPS) {
  Symbol* klass_name = SymbolTable::lookup(klass, (int)strlen(klass), CHECK_NULL);
  return SystemDictionary::resolve_or_fail(klass_name, _loader, _protection_domain, true, THREAD);
}

// Parse the standard tuple of <klass> <name> <signature>
Method* ciCacheProfiles::parse_method(TRAPS) {
  ResourceMark rm;
  InstanceKlass* k = (InstanceKlass*)parse_klass(CHECK_NULL);
  Symbol* method_name = parse_symbol(CHECK_NULL);
  Symbol* method_signature = parse_symbol(CHECK_NULL);
  Method* m = k->find_method(method_name, method_signature);
  if (m == NULL) {
    report_error("Can't find method");
  }
  return m;
}

int ciCacheProfiles::get_line(int c) {
  while(c != EOF) {
    if (_buffer_pos + 1 >= _buffer_length) {
      int new_length = _buffer_length * 2;
      // Next call will throw error in case of OOM.
      _buffer = REALLOC_RESOURCE_ARRAY(char, _buffer, _buffer_length, new_length);
      _buffer_length = new_length;
    }
    if (c == '\n') {
      c = getc(_stream); // get next char
      break;
    } else if (c == '\r') {
      // skip LF
    } else {
      _buffer[_buffer_pos++] = c;
    }
    c = getc(_stream);
  }
  // null terminate it, reset the pointer
  _buffer[_buffer_pos] = '\0'; // NL or EOF
  _buffer_pos = 0;
  _bufptr = _buffer;
  return c;
}

// Take an ascii string contain \u#### escapes and convert it to utf8
// in place.
void ciCacheProfiles::unescape_string(char* value) {
  char* from = value;
  char* to = value;
  while (*from != '\0') {
    if (*from != '\\') {
      *from++ = *to++;
    } else {
      switch (from[1]) {
        case 'u': {
          from += 2;
          jchar value=0;
          for (int i=0; i<4; i++) {
            char c = *from++;
            switch (c) {
              case '0': case '1': case '2': case '3': case '4':
              case '5': case '6': case '7': case '8': case '9':
                value = (value << 4) + c - '0';
                break;
              case 'a': case 'b': case 'c':
              case 'd': case 'e': case 'f':
                value = (value << 4) + 10 + c - 'a';
                break;
              case 'A': case 'B': case 'C':
              case 'D': case 'E': case 'F':
                value = (value << 4) + 10 + c - 'A';
                break;
              default:
                ShouldNotReachHere();
            }
          }
          UNICODE::convert_to_utf8(&value, 1, to);
          to++;
          break;
        }
        case 't': *to++ = '\t'; from += 2; break;
        case 'n': *to++ = '\n'; from += 2; break;
        case 'r': *to++ = '\r'; from += 2; break;
        case 'f': *to++ = '\f'; from += 2; break;
        default:
          ShouldNotReachHere();
      }
    }
  }
  *from = *to;
}

const char* ciCacheProfiles::error_message() {
  return _error_message;
}

// Process each line of the replay file executing each command until
// the file ends.
void ciCacheProfiles::process(TRAPS) {
  int line_no = 1;
  int c = getc(_stream);
  while(c != EOF) {
    c = get_line(c);
    process_command(THREAD);
    if (had_error()) {
      // flag this compile record to not use it
      _had_loading_error = true;
      // TODO: adding this code crashes JVM
      // also free currently used datastructure
//      for(int i = 0;i<_method_records_pos;i++) {
//        FREE_C_HEAP_OBJ(_method_records[i]);
//      }
//      for(int i = 0;i<_method_data_records_pos;i++) {
//        FREE_C_HEAP_OBJ(_method_data_records[i]);
//      }
      if(PrintCacheProfiles) {
        tty->print_cr("Error while parsing line %d: %s\n", line_no, _error_message);
      }
      if (CacheIgnoreInitErrors) {
        CLEAR_PENDING_EXCEPTION;
        _error_message = NULL;
      } else {
        return;
      }
    }
    line_no++;
  }
}

void ciCacheProfiles::process_command(TRAPS) {
  char* cmd = parse_string();
  if (cmd == NULL) {
    return;
  }
  if (strcmp("#", cmd) == 0) {
    // # means that a new record starts
    _imethod = NULL;
    _iklass  = NULL;
    // and continue as usual

    // TODO: parameterize init length
    _had_loading_error = false;
    _method_records_pos = 0;
    _method_records_length = 8;
    _method_data_records_pos = 0;
    _method_data_records_length = 8;
    _method_records = NEW_C_HEAP_ARRAY(MethodRecord*, _method_records_length, mtCompiler);
    _method_data_records = NEW_C_HEAP_ARRAY(MethodDataRecord*, _method_data_records_length, mtCompiler );
#if INCLUDE_JVMTI
  } else if (strcmp("JvmtiExport", cmd) == 0) {
    process_JvmtiExport(CHECK);
#endif // INCLUDE_JVMTI
  } else if (!_had_loading_error) {
    if (strcmp("compile", cmd) == 0) {
      process_compile(CHECK);
    } else if (strcmp("ciMethod", cmd) == 0) {
      process_ciMethod(CHECK);
    } else if (strcmp("ciMethodData", cmd) == 0) {
      process_ciMethodData(CHECK);
    } else {
      report_error("unknown command");
    }
  } else {
    // ignore command
  }
}


// new method, does something completely different
// instead of compiling it adds the compilations to the data structure during initialization
void ciCacheProfiles::process_compile(TRAPS) {
  char* klass_name = parse_string();
  char* method_name = parse_string();
  char* signature = parse_string();
  int entry_bci = parse_int("entry_bci");
  int comp_level = parse_int("comp_level");
  // old version w/o comp_level
  if (had_error() && (error_message() == "comp_level")) {
    comp_level = CompLevel_full_optimization;
  }

  CompileRecord* rec = new_compileRecord(klass_name, method_name, signature, comp_level);
  // new_compileRecord will return NULL if we should not overwrite an existing profile
  if(rec != NULL) {
    ResourceMark rm;

    rec->_entry_bci = entry_bci;
    rec->_comp_level = comp_level;
    int inline_count = 0;
    if (parse_tag_and_count("inline", inline_count)) {
      // Record inlining data
      rec->_inline_records = new GrowableArray<InlineRecord*>();
      for (int i = 0; i < inline_count; i++) {
        int depth = parse_int("inline_depth");
        int bci = parse_int("inline_bci");
        if (had_error()) {
          break;
        }
        Method* inl_method = parse_method(CHECK);
        if (had_error()) {
          break;
        }
        rec->new_inlineRecord(inl_method, bci, depth);
      }
    }
  }
}

// ciMethod <klass> <name> <signature> <invocation_counter> <backedge_counter> <interpreter_invocation_count> <interpreter_throwout_count> <instructions_size>
void ciCacheProfiles::process_ciMethod(TRAPS) {
  //Method* method = parse_method(CHECK);
  //if (had_error()) return;
  char* klass_name = parse_string();
  char* method_name = parse_string();
  char* signature = parse_string();
  MethodRecord* rec = new_methodRecord(klass_name,method_name,signature);

  ResourceMark rm;
  rec->_invocation_counter = parse_int("invocation_counter");
  rec->_backedge_counter = parse_int("backedge_counter");
  rec->_interpreter_invocation_count = parse_int("interpreter_invocation_count");
  rec->_interpreter_throwout_count = parse_int("interpreter_throwout_count");
  rec->_instructions_size = parse_int("instructions_size");
}

// ciMethodData <klass> <name> <signature> <state> <current mileage> orig <length> # # ... data <length> # # ... oops <length> # ... methods <length>
void ciCacheProfiles::process_ciMethodData(TRAPS) {
  char* klass_name = parse_string();
  char* method_name = parse_string();
  char* signature = parse_string();
  // collect and record all the needed information for later
  MethodDataRecord* rec = new_methodDataRecord(klass_name, method_name, signature);

  ResourceMark rm;

  rec->_state = parse_int("state");
  rec->_current_mileage = parse_int("current_mileage");

  rec->_orig_data = parse_data("orig", rec->_orig_data_length);
  if (rec->_orig_data == NULL) {
    return;
  }
  rec->_data = parse_intptr_data("data", rec->_data_length);
  if (rec->_data == NULL) {
    return;
  }
  if (!parse_tag_and_count("oops", rec->_classes_length)) {
    return;
  }
  rec->_classes = NEW_C_HEAP_ARRAY(Klass*, rec->_classes_length, mtCompiler);
  rec->_classes_offsets = NEW_C_HEAP_ARRAY(int, rec->_classes_length, mtCompiler);
  for (int i = 0; i < rec->_classes_length; i++) {
    int offset = parse_int("offset");
    if (had_error()) {
      return;
    }
    Klass* k = parse_klass(CHECK);
    rec->_classes_offsets[i] = offset;
    rec->_classes[i] = k;
  }

  if (!parse_tag_and_count("methods", rec->_methods_length)) {
    return;
  }
  rec->_methods = NEW_C_HEAP_ARRAY(Method*, rec->_methods_length, mtCompiler);
  rec->_methods_offsets = NEW_C_HEAP_ARRAY(int, rec->_methods_length, mtCompiler);
  for (int i = 0; i < rec->_methods_length; i++) {
    int offset = parse_int("offset");
    if (had_error()) {
      return;
    }
    Method* m = parse_method(CHECK);
    rec->_methods_offsets[i] = offset;
    rec->_methods[i] = m;
  }
}

#if INCLUDE_JVMTI
void ciCacheProfiles::process_JvmtiExport(TRAPS) {
  const char* field = parse_string();
  bool value = parse_int("JvmtiExport flag") != 0;
  if (strcmp(field, "can_access_local_variables") == 0) {
    JvmtiExport::set_can_access_local_variables(value);
  } else if (strcmp(field, "can_hotswap_or_post_breakpoint") == 0) {
    JvmtiExport::set_can_hotswap_or_post_breakpoint(value);
  } else if (strcmp(field, "can_post_on_exceptions") == 0) {
    JvmtiExport::set_can_post_on_exceptions(value);
  } else {
    report_error("Unrecognized JvmtiExport directive");
  }
}
#endif // INCLUDE_JVMTI

// initialize the cache profiler and parse the profile file
// to save methods in the ciCompileRecords array
void ciCacheProfiles::initialize(TRAPS) {
  if (!is_initialized()) {
    HandleMark hm;
    if (FLAG_IS_DEFAULT(CacheProfilesFile)) {
      tty->print_cr("NOTE: no explicit compiler cache profiles file specified, uses -XX:CacheProfilesFile=cached_profiles.dat.");
      CacheProfilesFile = "cached_profiles.dat";
    }

    // Load and parse the replay data
    // initialize variables (these were part of the cache before)
    _thread = THREAD;
    _loader = Handle(_thread, SystemDictionary::java_system_loader());
    _protection_domain = Handle();

    _stream = fopen(CacheProfilesFile, "rt");
    if (_stream == NULL) {
      fprintf(stderr, "ERROR: Can't open cache profile %s\n", CacheProfilesFile);
    }

    _error_message = NULL;

    _buffer_length = 32;
    _buffer = NEW_C_HEAP_ARRAY(char, _buffer_length, mtCompiler);
    _bufptr = _buffer;
    _buffer_pos = 0;

		_compile_records_pos = 0;
    _compile_records_length = 16;
    _compile_records = NEW_C_HEAP_ARRAY(CompileRecord*, _compile_records_length, mtCompiler );

    if (can_replay()) {
      process(THREAD);
    }

    if (HAS_PENDING_EXCEPTION) {
      oop throwable = PENDING_EXCEPTION;
      CLEAR_PENDING_EXCEPTION;
      java_lang_Throwable::print(throwable, tty);
      tty->cr();
      java_lang_Throwable::print_stack_trace(throwable, tty);
      tty->cr();
    }

    if (had_error()) {
      tty->print_cr("Failed on %s", error_message());
    }
  }
  is_initialized(true);
  if (PrintCacheProfiles) {
    tty->print_cr("CacheProfiles: CachedProfiles initialized!");
  }
}

bool ciCacheProfiles::is_initialized() {
  return _initialized;
}

void ciCacheProfiles::is_initialized(bool flag) {
  _initialized = flag;
}

// returns the complevel if cached, else 0
int ciCacheProfiles::is_cached(Method* method) {
  if (!is_initialized()) {
    return 0;
  }
  //VM_ENTRY_MARK;
  ASSERT_IN_VM;
  ResourceMark rm;
  CompileRecord* rec = find_compileRecord(method);
  if(rec == NULL) {
    return 0;
  } else {
    return rec->_comp_level;
  }
}

// same function for a method holder
int ciCacheProfiles::is_cached(methodHandle method) {
  if (!is_initialized()) {
    return 0;
  }
  //VM_ENTRY_MARK;
  ASSERT_IN_VM;
  ResourceMark rm;
  CompileRecord* rec = find_compileRecord(method);
  if(rec == NULL) {
    return 0;
  } else {
    return rec->_comp_level;
  }
}

// Create and initialize a record for a ciMethod
MethodRecord* ciCacheProfiles::new_methodRecord(char* klass_name, char* method_name, char* signature) {
  MethodRecord* rec = NEW_C_HEAP_OBJ(MethodRecord, mtCompiler);
  rec->setupMethodRecord(klass_name, method_name, signature);
  // if array has empty space
  if(_method_records_pos<_method_records_length) {
    _method_records[_method_records_pos] = rec;
    _method_records_pos++;
  } else {
    // grow
    MethodRecord** old_method_records = _method_records;
    int old_method_records_length = _method_records_length;
    _method_records_length = _method_records_length*2;
    _method_records = NEW_C_HEAP_ARRAY(MethodRecord*, _method_records_length, mtCompiler);
    for(int i = 0; i< old_method_records_length; i++) {
      _method_records[i] = old_method_records[i];
    }
    _method_records[_method_records_pos] = rec;
    _method_records_pos++;
    FREE_C_HEAP_OBJ(old_method_records);
  }
  return rec;
}

// Create and initialize a record for a ciMethodData
MethodDataRecord* ciCacheProfiles::new_methodDataRecord(char* klass_name, char* method_name, char* signature) {
  MethodDataRecord* rec = NEW_C_HEAP_OBJ(MethodDataRecord, mtCompiler);
  rec->setupMethodDataRecord(klass_name, method_name, signature);
  // if array has empty space
  if(_method_data_records_pos<_method_data_records_length) {
    _method_data_records[_method_data_records_pos] = rec;
    _method_data_records_pos++;
  } else {
    // grow
    MethodDataRecord** old_method_data_records = _method_data_records;
    int old_method_data_records_length = _method_data_records_length;
    _method_data_records_length = _method_data_records_length*2;
    _method_data_records = NEW_C_HEAP_ARRAY(MethodDataRecord*, _method_data_records_length, mtCompiler);
    for(int i = 0; i< old_method_data_records_length; i++) {
      _method_data_records[i] = old_method_data_records[i];
    }
    _method_data_records[_method_data_records_pos] = rec;
    _method_data_records_pos++;
    FREE_C_HEAP_OBJ(old_method_data_records);
  }
  return rec;
}

// Create and initialize a record for a ciCompile
CompileRecord* ciCacheProfiles::new_compileRecord(char* klass_name, char* method_name, char* signature, int comp_level) {
  CompileRecord* rec = find_compileRecord(klass_name, method_name, signature);
  // if we don't have a record yet, save to compile record array
  if(rec == NULL) {
    rec = NEW_C_HEAP_OBJ(CompileRecord, mtCompiler);
    rec->setupCompileRecord(klass_name, method_name, signature);
    // if array has empty space
    if(_compile_records_pos<_compile_records_length) {
      _compile_records[_compile_records_pos] = rec;
      _compile_records_pos++;
    } else {
      // grow
      CompileRecord** old_compile_records = _compile_records;
      int old_compile_records_length = _compile_records_length;
      _compile_records_length = _compile_records_length*2;
      _compile_records = NEW_C_HEAP_ARRAY(CompileRecord*, _compile_records_length, mtCompiler);
      for(int i = 0; i< old_compile_records_length; i++) {
        _compile_records[i] = old_compile_records[i];
      }
      _compile_records[_compile_records_pos] = rec;
      _compile_records_pos++;
      FREE_C_HEAP_OBJ(old_compile_records);
    }
  // only save if our profile is from a higher compiler tier
  } else if (comp_level >= rec->_comp_level) {
    // we already have a record, so we should free the old datastructure
    rec->_iklass = NULL;
    rec->_imethod = NULL;
    for(int i = 0;i<rec->_method_records_pos;i++) {
      FREE_C_HEAP_OBJ(rec->_method_records[i]);
    }
    for(int i = 0;i<rec->_method_data_records_pos;i++) {
      FREE_C_HEAP_OBJ(rec->_method_data_records[i]);
    }
    rec->setupCompileRecord(klass_name, method_name, signature);
  // this means our compile level is smaller than the cached one
  // and we don't want to overwrite the profile in this case
  // free temporary datastructure
  } else {
    _iklass = NULL;
    _imethod = NULL;
    for(int i = 0;i<_method_records_pos;i++) {
      FREE_C_HEAP_OBJ(_method_records[i]);
    }
    for(int i = 0;i<_method_data_records_pos;i++) {
      FREE_C_HEAP_OBJ(_method_data_records[i]);
    }
    return NULL;
  }
  // save temporary datastructures to the found or newly created CompileRecord
  rec->_iklass = _iklass;
  rec->_imethod = _imethod;
  rec->_method_records = _method_records;
  rec->_method_data_records = _method_data_records;

  // overwrite length and pos with new values
  rec->_method_records_pos = _method_records_pos;
  rec->_method_data_records_pos = _method_data_records_pos;
  rec->_method_records_length = _method_records_length;
  rec->_method_data_records_length = _method_data_records_length;
  return rec;
}

// Lookup data for a ciMethod
CompileRecord* ciCacheProfiles::find_compileRecord(Method* method) {
  char* klass_name =  method->method_holder()->name()->as_utf8();
  char* method_name = method->name()->as_utf8();
  char* signature = method->signature()->as_utf8();
  for (int i = 0; i < _compile_records_pos; i++) {
    CompileRecord* rec = _compile_records[i];
    if (strcmp(rec->_klass_name, klass_name) == 0 &&
        strcmp(rec->_method_name, method_name) == 0 &&
        strcmp(rec->_signature, signature) == 0) {
      return rec;
    }
  }
  return NULL;
}
// Lookup data for a methodHandle
CompileRecord* ciCacheProfiles::find_compileRecord(methodHandle mh) {
  char* klass_name =  mh->method_holder()->name()->as_utf8();
  char* method_name = mh->name()->as_utf8();
  char* signature = mh->signature()->as_utf8();
  for (int i = 0; i < _compile_records_pos; i++) {
    CompileRecord* rec = _compile_records[i];
    if (strcmp(rec->_klass_name, klass_name) == 0 &&
        strcmp(rec->_method_name, method_name) == 0 &&
        strcmp(rec->_signature, signature) == 0) {
      return rec;
    }
  }
  return NULL;
}
// Lookup data for a ciMethod
CompileRecord* ciCacheProfiles::find_compileRecord(char* klass_name, char* method_name, char* signature) {
  for (int i = 0; i < _compile_records_pos; i++) {
    CompileRecord* rec = _compile_records[i];
    if (strcmp(rec->_klass_name, klass_name) == 0 &&
        strcmp(rec->_method_name, method_name) == 0 &&
        strcmp(rec->_signature, signature) == 0) {
      return rec;
    }
  }
  return NULL;
}

//void ciCacheProfiles::reset() {
//  _error_message = NULL;
//  _method_records.clear();
//  _method_data_records->clear();
//  _compile_records->clear();
//}
//#endif // PRODUCT
