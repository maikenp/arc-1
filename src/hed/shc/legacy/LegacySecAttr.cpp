#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "LegacySecAttr.h"


namespace ArcSHCLegacy {


static std::string empty_string;
static std::list<std::string> empty_list;


LegacySecAttr::LegacySecAttr(Arc::Logger& logger):logger_(logger) {
}

LegacySecAttr::~LegacySecAttr(void) {
}

std::string LegacySecAttr::get(const std::string& id) const {
  if(id == "GROUP") {
    if(groups_.size() > 0) return *groups_.begin();
    return "";
  };
  if(id == "VO") {
    if(VOs_.size() > 0) return *VOs_.begin();
    return "";
  };
  return "";
}

std::list<std::string> LegacySecAttr::getAll(const std::string& id) const {
  if(id == "GROUP") return groups_;
  if(id == "VO") return VOs_;
  return std::list<std::string>();
}


bool LegacySecAttr::Export(Arc::SecAttrFormat format,Arc::XMLNode &val) const {
  // No need to export information yet.
  return true;
}

bool LegacySecAttr::equal(const SecAttr &b) const {
  try {
    const LegacySecAttr& a = dynamic_cast<const LegacySecAttr&>(b);
    if (!a) return false;
    // ...
    return false;
  } catch(std::exception&) { };
  return false;
}

LegacySecAttr::operator bool(void) const {
  return true;
}

const std::list<std::string>& LegacySecAttr::GetGroupVO(const std::string& group) const {
  std::list< std::list<std::string> >::const_iterator vo = groupsVO_.begin();
  for(std::list<std::string>::const_iterator grp = groups_.begin(); grp != groups_.end(); ++grp) {
    if(vo == groupsVO_.end()) break;
    if(*grp == group) return *vo;
    ++vo;
  };
  return empty_list;
}

const std::list<std::string>& LegacySecAttr::GetGroupVOMS(const std::string& group) const {
  std::list< std::list<std::string> >::const_iterator voms = groupsVOMS_.begin();
  for(std::list<std::string>::const_iterator grp = groups_.begin(); grp != groups_.end(); ++grp) {
    if(voms == groupsVOMS_.end()) break;
    if(*grp == group) return *voms;
    ++voms;
  };
  return empty_list;
}

void LegacySecAttr::AddGroup(const std::string& group, const std::list<std::string>& vo, const std::list<std::string>& voms) {
  groups_.push_back(group);
  groupsVO_.push_back(vo);
  groupsVOMS_.push_back(voms);
}


} // namespace ArcSHCLegacy

