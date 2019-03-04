#ifndef PROJECT_TOTUS_POSTGRESQL_HPP
#define PROJECT_TOTUS_POSTGRESQL_HPP

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
};

extern PostgreSQL *postgresql;

}

#endif
