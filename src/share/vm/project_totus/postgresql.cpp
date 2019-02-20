#include "postgresql.hpp"

#include <cassert>
#include <list>
#include <vector>

#include <arpa/inet.h>
#include <libpq-fe.h>

#include <stdlib.h> // for exit, TODO replace with VMError::report_and_die()

using namespace project_totus;

namespace project_totus {

struct PostgreSQLImpl {
  PGconn *Connection;
  PGresult *Result;
};

}

namespace {

class Params {
  std::vector<const char *> Values;
  std::list<uint32_t> BinaryValues; // Required for stable iterators
  std::vector<int> Lengths;
  std::vector<int> Formats;
public:
  void addText(const char *Text) {
    Values.push_back(Text);
    Lengths.push_back(0); // Ignored for text, used for binary
    Formats.push_back(0); // 0 is text, 1 is binary
  }
  void addBinary(uint32_t Binary) {
    BinaryValues.push_back(htonl(Binary));
    auto &BinaryValue = BinaryValues.back();
    const char *Value = (const char *) &BinaryValue;

    Values.push_back(Value);
    Lengths.push_back(sizeof(BinaryValue));
    Formats.push_back(1); // 1 is binary
  }
  void addBool(bool B) {
    if (B)
      addText("true");
    else
      addText("false");
  }
  void clear() {
    Values.clear();
    BinaryValues.clear();
    Lengths.clear();
    Formats.clear();
  }

  const char * const * getValues() const {
    return Values.data();
  }
  const int * getLengths() const {
    return Lengths.data();
  }
  const int * getFormats() const {
    return Formats.data();
  }
  int getN() const {
    if (Values.size() != Lengths.size()) {
      exit(-1);
    }
    if (Values.size() != Formats.size()) {
      exit(-1);
    }
    return Values.size();
  }
};

void Exec(PostgreSQLImpl &Impl, const char *Query, const Params &P) {
  Impl.Result = PQexecParams(Impl.Connection, Query, P.getN(), nullptr,
			     P.getValues(), P.getLengths(), P.getFormats(), 1);
}

void ExecCommand(PostgreSQLImpl &Impl,
		 const char *Query,
		 const Params &Params) {
  Exec(Impl, Query, Params);
  if (PQresultStatus(Impl.Result) != PGRES_COMMAND_OK) {
    exit(-1);
  }
  PQclear(Impl.Result);
}

void ExecTuples(PostgreSQLImpl &Impl,
		const char *Query,
		const Params &Params) {
  Exec(Impl, Query, Params);
  if (PQresultStatus(Impl.Result) != PGRES_TUPLES_OK) {
    exit(-1);
  }
}

uint32_t GetID(PostgreSQLImpl &Impl,
	       const char *Query,
	       const Params &Params) {
  ExecTuples(Impl, Query, Params);
  if (PQntuples(Impl.Result) != 1) {
    exit(-1);
  }
  int FieldIndex = PQfnumber(Impl.Result, "id");
  char *Value = PQgetvalue(Impl.Result, 0, FieldIndex);
  uint32_t ID = ntohl(*((uint32_t *) Value));
  PQclear(Impl.Result);
  return ID;
}

}

PostgreSQL::PostgreSQL(const std::string &PackageName,
		       const std::string &PackageVersion)
    : Impl(new PostgreSQLImpl) {
  Impl->Connection = PQconnectdb("dbname=project_totus");
  if (PQstatus(Impl->Connection) != CONNECTION_OK) {
    exit(-1);
  }
  Params Params;
  Params.addText("project_totus");
  Params.addText("0001_initial");
  ExecTuples(
    *Impl,
    "SELECT id FROM django_migrations WHERE app = $1 AND name = $2;",
    Params);
  if (PQntuples(Impl->Result) != 1) {
    exit(-1);
  }
  PQclear(Impl->Result);
  Params.clear();
  Params.addText(PackageName.c_str());
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_package_base (name) VALUES ($1)"
    " ON CONFLICT DO NOTHING;",
    Params);
  uint32_t PackageNameID = GetID(
    *Impl,
    "SELECT id FROM project_totus_package_base WHERE name = $1;",
    Params);
  Params.clear();
  Params.addBinary(PackageNameID);
  Params.addText(PackageVersion.c_str());
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_package (base_id, version) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING;",
    Params);
  PackageID = GetID(
    *Impl,
    "SELECT id FROM project_totus_package"
    " WHERE base_id = $1 AND version = $2;",
    Params);
}

PostgreSQL::~PostgreSQL() {
  PQfinish(Impl->Connection);
}
