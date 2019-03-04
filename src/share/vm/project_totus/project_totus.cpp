#include "project_totus.hpp"

#include "postgresql.hpp"

#include "runtime/os.hpp"
#include "utilities/ostream.hpp"

#include <stdio.h>

void project_totus::initialize()
{
    const char *package_name = getenv("PROJECT_TOTUS_PACKAGE_NAME");
    if (package_name == nullptr) {
        printf("ERROR: environment variable PROJECT_TOTUS_PACKAGE_NAME not set\n");
        os::abort();
    }
    const char *package_version = getenv("PROJECT_TOTUS_PACKAGE_VERSION");
    if (package_version == nullptr) {
        printf("ERROR: environment variable PROJECT_TOTUS_PACKAGE_VERSION not set\n");
        os::abort();
    }
    project_totus::postgresql = new PostgreSQL(package_name, package_version);
}
