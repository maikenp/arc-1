#ifndef __GM_CONFIG_MAP_H__
#define __GM_CONFIG_MAP_H__

#include <arc/data/URLMap.h>

#include "environment.h"

/*
  Look URLMap.h for functionality.
  This object automatically reads configuration file
  and fills list of mapping for UrlMap.
*/
class UrlMapConfig: public Arc::URLMap {
 public:
  UrlMapConfig(GMEnvironment& env);
  ~UrlMapConfig(void);
};

#endif // __GM_CONFIG_MAP_H__
