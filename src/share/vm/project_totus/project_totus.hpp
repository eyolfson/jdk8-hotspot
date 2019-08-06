#ifndef PROJECT_TOTUS_PROJECT_TOTUS_HPP
#define PROJECT_TOTUS_PROJECT_TOTUS_HPP

#include "postgresql.hpp"

namespace project_totus {

void setDebug();
void incIndent();
void decIndent();
int getIndent();
void unsetDebug();
bool isDebug();

void initialize();

bool isDisabled();
bool isRecordingInlineSet();
bool isUsingInlineSet();
bool isRecordingC2EarlyCompile();
bool isUsingC2EarlyCompile();

PostgreSQL * getDatabase();

}

#endif
