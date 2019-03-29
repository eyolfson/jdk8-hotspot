#include "project_totus.hpp"

#include "postgresql.hpp"

#include "runtime/os.hpp"
#include "utilities/ostream.hpp"

#include <stdio.h>

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
