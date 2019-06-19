#ifndef PROJECT_TOTUS_POSTGRESQL_HPP
#define PROJECT_TOTUS_POSTGRESQL_HPP

#include "ci/ciMethod.hpp"

#undef max
#undef min

#include <memory>
#include <string>

// #define DEBUG_CALLER_KLASS_NAME "scala/runtime/BoxesRunTime"
// #define DEBUG_CALLER_BCI 70

namespace project_totus {

class PostgreSQLImpl;

class PostgreSQL {
  std::unique_ptr<PostgreSQLImpl> Impl;
  uint32_t PackageID;
  uint32_t ExperimentID;
  uint32_t InlineSetID;
public:
  PostgreSQL(const std::string &PackageName,
	     const std::string &PackageVersion,
	     const std::string &ExperimentName);
  ~PostgreSQL();

  void populateInlineSetID(const std::string &InlineSetName);
  void populateInlineSetMethodCalls();
  void populateEarlyC2CompileMethods();

  uint32_t getInlineMethodCallID(ciMethod *caller,
				 int bci,
				 ciMethod * callee);
  void addInlineDecision(uint32_t inline_method_call_id,
			 bool require_inline);
  bool useInlineSet() const { return InlineSetID != 0; }
  bool forceInline(ciMethod* caller,
		   int bci,
		   ciMethod* callee);
  void addC2CompileMethod(Method* method, int bci);
  bool shouldC2CompileMethod(Method* method, int bci);
private:
  uint32_t getCallSiteID(ciMethod* method, int bci);
  uint32_t getMethodID(const char* klass_name,
		       const char* method_name,
		       const char* method_signature,
		       bool method_is_static,
		       int method_code_size);
  uint32_t getMethodID(ciMethod* ci_method);
  uint32_t getMethodID(Method* method);
  uint32_t getMethodCallID(uint32_t CallSiteID, uint32_t CalleeID);
  uint32_t getInlineMethodCallID(uint32_t MethodCallID);
};

}

#endif
