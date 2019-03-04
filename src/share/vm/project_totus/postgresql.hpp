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
public:
  PostgreSQL(const std::string &PackageName, const std::string &PackageVersion);
  ~PostgreSQL();
  void addMethod(ciMethod * method);
};

extern PostgreSQL *postgresql;

}

#endif
