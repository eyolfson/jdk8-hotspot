#include "project_totus.hpp"

#include "classfile/systemDictionary.hpp"
#include "runtime/os.hpp"
#include "utilities/debug.hpp"
#include "utilities/ostream.hpp"

#include "runtime/thread.hpp"

#include <stdio.h>

namespace {
  enum ModeKind {
    MK_DISABLED,
    MK_RECORDING_INLINE_SET,
    MK_USING_INLINE_SET,
    MK_RECORDING_C2_EARLY_COMPILE,
    MK_USING_C2_EARLY_COMPILE,
  };
  ModeKind Mode;
  project_totus::PostgreSQL * Database;
}

void project_totus::initialize()
{
  const char * EnvMode = getenv("PROJECT_TOTUS_MODE");
  if (EnvMode == nullptr) {
    ShouldNotReachHere();
  }

  if (strcasecmp(EnvMode, "disabled") == 0) {
    Mode = MK_DISABLED;
    Database = nullptr;
    return;
  }
  else if (strcasecmp(EnvMode, "recording_inline_set") == 0) {
    Mode = MK_RECORDING_INLINE_SET;
  }
  else if (strcasecmp(EnvMode, "using_inline_set") == 0) {
    Mode = MK_USING_INLINE_SET;
  }
  else if (strcasecmp(EnvMode, "recording_c2_early_compile") == 0) {
    Mode = MK_RECORDING_C2_EARLY_COMPILE;
  }
  else if (strcasecmp(EnvMode, "using_c2_early_compile") == 0) {
    Mode = MK_USING_C2_EARLY_COMPILE;
  }
  else {
    dprintf(1, "%s\n", EnvMode);
    ShouldNotReachHere();
  }

  const char * PackageName = getenv("PROJECT_TOTUS_PACKAGE_NAME");
  if (PackageName == nullptr) {
    ShouldNotReachHere();
  }
  const char * PackageVersion = getenv("PROJECT_TOTUS_PACKAGE_VERSION");
  if (PackageVersion == nullptr) {
    ShouldNotReachHere();
  }
  const char * ExperimentName = getenv("PROJECT_TOTUS_EXPERIMENT_NAME");
  if (ExperimentName == nullptr) {
    ShouldNotReachHere();
  }
  Database = new PostgreSQL(PackageName,
			    PackageVersion,
			    ExperimentName);

  if (Mode == MK_RECORDING_INLINE_SET || Mode == MK_USING_INLINE_SET) {
    const char * InlineSetName = getenv("PROJECT_TOTUS_INLINE_SET_NAME");
    if (InlineSetName == nullptr) {
      ShouldNotReachHere();
    }
    Database->populateInlineSetID(InlineSetName);
    if (Mode == MK_USING_INLINE_SET) {
      Database->populateInlineSetMethodCalls();
    }
  }
  else if (Mode == MK_USING_C2_EARLY_COMPILE) {
    Database->populateEarlyC2CompileMethods();
  }
}

bool project_totus::isDisabled() {
  return Mode == MK_DISABLED;
}

bool project_totus::isRecordingInlineSet() {
 return Mode == MK_RECORDING_INLINE_SET;
}

bool project_totus::isUsingInlineSet() {
  return Mode == MK_USING_INLINE_SET;
}

bool project_totus::isRecordingC2EarlyCompile() {
  return Mode == MK_RECORDING_C2_EARLY_COMPILE;
}

bool project_totus::isUsingC2EarlyCompile() {
  return Mode == MK_USING_C2_EARLY_COMPILE;
}

project_totus::PostgreSQL * project_totus::getDatabase() {
  return Database;
}
