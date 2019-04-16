#include "project_totus.hpp"

#include "runtime/os.hpp"
#include "utilities/ostream.hpp"

#include <stdio.h>


project_totus::PostgreSQL *project_totus::postgresql = nullptr;


void project_totus::initialize()
{
    if (getenv("PROJECT_TOTUS_DISABLE") != nullptr) {
        return;
    }
    const char *package_name = getenv("PROJECT_TOTUS_PACKAGE_NAME");
    if (package_name == nullptr) {
        return;
    }
    const char *package_version = getenv("PROJECT_TOTUS_PACKAGE_VERSION");
    if (package_version == nullptr) {
        return;
    }
    const char *experiment_name = getenv("PROJECT_TOTUS_EXPERIMENT_NAME");
    if (experiment_name == nullptr) {
        return;
    }
    const char *inline_set_name = getenv("PROJECT_TOTUS_INLINE_SET_NAME");
    project_totus::postgresql = new PostgreSQL(package_name,
					       package_version,
					       experiment_name,
					       inline_set_name);
}

bool project_totus::is_recording()
{
  return project_totus::postgresql
         && !project_totus::postgresql->useInlineSet();
}

bool project_totus::is_using_inline_set()
{
  return project_totus::postgresql
         && project_totus::postgresql->useInlineSet();
}
