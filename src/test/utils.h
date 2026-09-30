/**
 * swr - a software rasterizer
 *
 * Test utilities.
 *
 * \author Felix Lubbe
 * \copyright Copyright (c) 2026
 * \license Distributed under the MIT software license (see accompanying LICENSE.txt).
 */

#include <ostream>

#include "swr/swr.h"

namespace swr
{

inline std::ostream& operator<<(
  std::ostream& os,
  error e)
{
    switch(e)
    {
    case error::none: return os << "error::none";
    case error::invalid_value: return os << "error::invalid_value";
    case error::invalid_operation: return os << "error::invalid_operation";
    case error::unimplemented: return os << "error::unimplemented";
    }

    return os << "error::<" << static_cast<int>(e) << ">";
}

}    // namespace swr
