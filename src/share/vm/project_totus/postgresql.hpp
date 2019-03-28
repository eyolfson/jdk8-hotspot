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
public:
  PostgreSQL(const std::string &PackageName,
	     const std::string &PackageVersion,
	     const std::string &ExperimentName);
  ~PostgreSQL();
  void addInlineDecision(ciMethod *caller,
			 int bci,
			 ciMethod * callee,
			 bool require_inline);
private:
  uint32_t getCallSiteID(ciMethod * method, int bci);
  uint32_t getMethodID(ciMethod * method);
  uint32_t getMethodCallID(uint32_t CallSiteID, uint32_t CalleeID);
  uint32_t getInlineMethodCallID(uint32_t MethodCallID);
};

extern PostgreSQL *postgresql;

}

#endif
