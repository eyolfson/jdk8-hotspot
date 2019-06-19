#ifndef PROJECT_TOTUS_PROJECT_TOTUS_HPP
#define PROJECT_TOTUS_PROJECT_TOTUS_HPP

#include "postgresql.hpp"

namespace project_totus {

void initialize();

bool isDisabled();
bool isRecordingInlineSet();
bool isUsingInlineSet();
bool isRecordingC2EarlyCompile();
bool isUsingC2EarlyCompile();

PostgreSQL * getDatabase();

}

#endif
