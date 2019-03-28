#include "postgresql.hpp"

#include "runtime/os.hpp"

#undef max
#undef min

#include <list>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <libpq-fe.h>

using namespace project_totus;

namespace project_totus {

PostgreSQL *postgresql = nullptr;

struct PostgreSQLImpl {
  PGconn *Connection;
  std::unordered_map<ciMethod *, uint32_t> MethodIDMap;
  std::unordered_map<ciMethod *, uint32_t> InlineMethodIDMap;
};

}

namespace {

pthread_mutex_t exec_mutex = PTHREAD_MUTEX_INITIALIZER;

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
      printf("ERROR: database parameters mismatched\n");
      os::abort();
    }
    if (Values.size() != Formats.size()) {
      printf("ERROR: database parameters mismatched\n");
      os::abort();
    }
    return Values.size();
  }
};

PGresult * Exec(PostgreSQLImpl &Impl,
		const char *Query,
		const Params &P) {
  pthread_mutex_lock(&exec_mutex);
  auto Result = PQexecParams(Impl.Connection, Query, P.getN(), nullptr,
			     P.getValues(), P.getLengths(), P.getFormats(), 1);
  pthread_mutex_unlock(&exec_mutex);
  return Result;
}

void ExecCommand(PostgreSQLImpl &Impl,
		 const char *Query,
		 const Params &Params) {
  auto Result = Exec(Impl, Query, Params);
  auto ResultStatus = PQresultStatus(Result);
  if (ResultStatus != PGRES_COMMAND_OK) {
    printf("ERROR: database did not execute command: %s\n",
	   PQresultErrorMessage(Result));
    os::abort();
  }
  PQclear(Result);
}

PGresult * ExecTuples(PostgreSQLImpl &Impl,
		      const char *Query,
		      const Params &Params) {
  auto Result = Exec(Impl, Query, Params);
  auto ResultStatus = PQresultStatus(Result);
  if (ResultStatus != PGRES_TUPLES_OK) {
    printf("ERROR: database did not return valid tuples: %s\n",
	   PQresultErrorMessage(Result));
    os::abort();
  }
  return Result;
}

uint32_t GetID(PostgreSQLImpl &Impl,
	       const char *Query,
	       const Params &Params) {
  auto Result = ExecTuples(Impl, Query, Params);
  if (PQntuples(Result) != 1) {
    printf("ERROR: database returned more than one ID\n");
    os::abort();
  }
  int FieldIndex = PQfnumber(Result, "id");
  char *Value = PQgetvalue(Result, 0, FieldIndex);
  uint32_t ID = ntohl(*((uint32_t *) Value));
  PQclear(Result);
  return ID;
}

}

PostgreSQL::PostgreSQL(const std::string &PackageName,
		       const std::string &PackageVersion,
		       const std::string &ExperimentName)
    : Impl(new PostgreSQLImpl) {
  Impl->Connection = PQconnectdb("dbname=project_totus");
  if (PQstatus(Impl->Connection) != CONNECTION_OK) {
    printf("ERROR: can not connect to database\n");
    os::abort();
  }
  Params Params;
  Params.addText("project_totus");
  Params.addText("0005_remove_timestamp");
  auto Result = ExecTuples(
    *Impl,
    "SELECT id FROM django_migrations WHERE app = $1 AND name = $2;",
    Params);
  if (PQntuples(Result) != 1) {
    printf("ERROR: apply the latest database migration\n");
    os::abort();
  }
  PQclear(Result);
  Params.clear();

  Params.addText(PackageName.c_str());
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_package_base (name) VALUES ($1)"
    " ON CONFLICT DO NOTHING",
    Params);
  uint32_t PackageNameID = GetID(
    *Impl,
    "SELECT id FROM project_totus_package_base WHERE name = $1",
    Params);
  Params.clear();

  Params.addBinary(PackageNameID);
  Params.addText(PackageVersion.c_str());
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_package (base_id, version) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING",
    Params);
  PackageID = GetID(
    *Impl,
    "SELECT id FROM project_totus_package"
    " WHERE base_id = $1 AND version = $2",
    Params);
  Params.clear();

  Params.addBinary(PackageID);
  Params.addText(ExperimentName.c_str());
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_experiment (package_id, name) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING",
    Params);
  ExperimentID = GetID(
    *Impl,
    "SELECT id FROM project_totus_experiment"
    " WHERE package_id = $1 AND name = $2",
    Params);
  Params.clear();
}

PostgreSQL::~PostgreSQL() {
  PQfinish(Impl->Connection);
}

uint32_t PostgreSQL::getMethodID(ciMethod * method)
{
  if (Impl->MethodIDMap.count(method) == 0) {
    Params Params;
    Params.addBinary(PackageID);
    Params.addText(method->holder()->name()->as_utf8());
    ExecCommand(
      *Impl,
      "INSERT INTO project_totus_klass (package_id, name) VALUES ($1, $2)"
      " ON CONFLICT DO NOTHING",
      Params);
    uint32_t KlassID = GetID(
      *Impl,
      "SELECT id FROM project_totus_klass"
      " WHERE package_id = $1 AND name = $2",
      Params);
    Params.clear();
    Params.addBinary(KlassID);
    Params.addText(method->name()->as_utf8());
    Params.addText(method->signature()->as_symbol()->as_utf8());
    Params.addBool(!method->is_static());
    Params.addBinary(method->code_size());
    ExecCommand(
      *Impl,
      "INSERT INTO project_totus_method"
      " (klass_id, name, descriptor, is_instance_method, size) VALUES ($1, $2, $3, $4, $5)"
      " ON CONFLICT DO NOTHING",
      Params);
    Params.clear();

    Params.addBinary(KlassID);
    Params.addText(method->name()->as_utf8());
    Params.addText(method->signature()->as_symbol()->as_utf8());
    uint32_t MethodID = GetID(
      *Impl,
      "SELECT id FROM project_totus_method"
      " WHERE klass_id = $1 AND name = $2 AND descriptor = $3",
      Params);
    Params.clear();
    Impl->MethodIDMap[method] = MethodID;
  }
  return Impl->MethodIDMap[method];
}

uint32_t PostgreSQL::getCallSiteID(ciMethod * caller, int bci)
{
  uint32_t CallerID = getMethodID(caller);
  Params Params;
  Params.addBinary(CallerID);
  Params.addBinary(bci);
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_call_site (caller_id, bci) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING",
    Params);
  uint32_t CallSiteID = GetID(
    *Impl,
    "SELECT id FROM project_totus_call_site"
    " WHERE caller_id = $1 AND bci = $2",
    Params);
  Params.clear();
  return CallSiteID;
}

uint32_t PostgreSQL::getMethodCallID(uint32_t CallSiteID, uint32_t CalleeID)
{
  Params Params;
  Params.addBinary(CallSiteID);
  Params.addBinary(CalleeID);
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_method_call (call_site_id, callee_id) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING",
    Params);
  uint32_t MethodCallID = GetID(
    *Impl,
    "SELECT id FROM project_totus_method_call"
    " WHERE call_site_id = $1 AND callee_id = $2",
    Params);
  Params.clear();
  return MethodCallID;
}

uint32_t PostgreSQL::getInlineMethodCallID(uint32_t MethodCallID)
{
  Params Params;
  Params.addBinary(ExperimentID);
  Params.addBinary(MethodCallID);
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_inline_method_call (experiment_id, method_call_id) VALUES ($1, $2)"
    " ON CONFLICT DO NOTHING",
    Params);
  uint32_t InlineMethodCallID = GetID(
    *Impl,
    "SELECT id FROM project_totus_inline_method_call"
    " WHERE experiment_id = $1 AND method_call_id = $2",
    Params);
  Params.clear();
  return InlineMethodCallID;
}

void PostgreSQL::addInlineDecision(ciMethod *caller,
				   int bci,
				   ciMethod * callee,
				   bool require_inline)
{
  uint32_t CallSiteID = getCallSiteID(caller, bci);
  uint32_t CalleeID = getMethodID(callee);
  uint32_t MethodCallID = getMethodCallID(CallSiteID, CalleeID);
  uint32_t InlineMethodCallID = getInlineMethodCallID(MethodCallID);

  Params Params;
  Params.addBinary(InlineMethodCallID);
  Params.addBool(require_inline);
  ExecCommand(
    *Impl,
    "INSERT INTO project_totus_inline_decision (inline_method_call_id, require_inline) VALUES ($1, $2)",
    Params);
  Params.clear();
}
