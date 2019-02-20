#ifndef PROJECT_TOTUS_POSTGRESQL_HPP
#define PROJECT_TOTUS_POSTGRESQL_HPP

// #include <cstdint>
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

}

#endif
