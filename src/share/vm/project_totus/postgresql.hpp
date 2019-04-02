#ifndef PROJECT_TOTUS_POSTGRESQL_HPP
#define PROJECT_TOTUS_POSTGRESQL_HPP

#include "ci/ciMethod.hpp"

#undef max
#undef min

#include <memory>
#include <string>

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
	     const std::string &ExperimentName,
	     const char *InlineSetName);
  ~PostgreSQL();
  uint32_t getInlineMethodCallID(ciMethod *caller,
				 int bci,
				 ciMethod * callee);
  void addInlineDecision(uint32_t inline_method_call_id,
			 bool require_inline);
  bool useInlineSet() const { return InlineSetID != 0; }
  bool forceInline(ciMethod *caller,
		   int bci,
		   ciMethod * callee);
private:
  uint32_t getCallSiteID(ciMethod * method, int bci);
  uint32_t getMethodID(ciMethod * method);
  uint32_t getMethodCallID(uint32_t CallSiteID, uint32_t CalleeID);
  uint32_t getInlineMethodCallID(uint32_t MethodCallID);
};

extern PostgreSQL *postgresql;

}

#endif
