#ifndef COA_PORTABLE_IMPORT_H
#define COA_PORTABLE_IMPORT_H

#include "Define.h"
#include <string>

namespace CoAPortableImport
{
    inline constexpr uint32 JobFormat = 2;
    inline constexpr uint32 CharacterFormat = 2;

    bool ValidJobId(std::string const& text);
    std::string Run(std::string const& jobId);
    std::string Project(std::string const& jobId);
}

#endif
