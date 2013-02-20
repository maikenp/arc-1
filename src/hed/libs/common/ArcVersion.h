// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_ARCVERSION_H__
#define __ARC_ARCVERSION_H__

/** \defgroup common Common utility classes and functions. */

/// \file ArcVersion.h
/** \addtogroup common
 * @{
 */
/** ARC API version */
#define ARC_VERSION "3.0.0"
/** ARC API version number */
#define ARC_VERSION_NUM 0x030000
/** ARC API major version number */
#define ARC_VERSION_MAJOR 3
/** ARC API minor version number */
#define ARC_VERSION_MINOR 0
/** ARC API patch number */
#define ARC_VERSION_PATCH 0

/// Arc namespace contains all core ARC classes.
namespace Arc {

  /// Determines ARC HED libraries version at runtime
  /**
   * ARC also provides pre-processor macros to determine the API version at
   * compile time in \ref ArcVersion.h.
   * \ingroup common
   * \headerfile ArcVersion.h arc/ArcVersion.h
   */
  class ArcVersion {
  public:
    /// Major version number
    const unsigned int Major;
    /// Minor version number
    const unsigned int Minor;
    /// Patch version number
    const unsigned int Patch;
    /// Parses ver and fills major, minor and patch version values
    ArcVersion(const char* ver);
  };

  /// Use this object to obtain current ARC HED version 
  /// at runtime.
  extern const ArcVersion Version;

  // Front page for ARC SDK documentation
  /**
   * \mainpage
   * The ARC %Software Development Kit (SDK) is a set of tools that allow
   * manipulation of jobs and data in a Grid environment. The SDK is divided
   * into a set of <a href="modules.html">modules</a> which take care of
   * different aspects of Grid interaction.
   *
   * \section sec Quick Start
   * The following code is a minimal example showing how to submit a job to a
   * Grid resource using the ARC SDK:
   * \include basic_job_submission.cpp
   * This code can be compiled with
   * \code
   * g++ -o submit -I/usr/include/libxml2 `pkg-config --cflags glibmm-2.4` -l arccompute submit.cpp
   * \endcode
   *
   * And this example shows how to copy a file to or from the Grid:
   * \include simple_copy.cpp
   *
   * This example can be compiled with
   * \code
   * g++ -o copy -I/usr/include/libxml2 `pkg-config --cflags glibmm-2.4` -l arcdata copy.cpp
   * \endcode
   *
   * \version The version of the SDK that this documentation refers to can be
   * found from #ARC_VERSION.
   */


} // namespace Arc

/** @} */
#endif // __ARC_ARCVERSION_H__
