#ifndef PROJECT_TOTUS_PROJECT_TOTUS_HPP
#define PROJECT_TOTUS_PROJECT_TOTUS_HPP

#include "postgresql.hpp"

namespace project_totus {

extern PostgreSQL *postgresql;

void initialize();
bool is_recording();
bool is_using_inline_set();

}

#endif
