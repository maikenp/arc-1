#ifndef __ARC_SEC_X500NAMEATTRIBUTE_H__
#define __ARC_SEC_X500NAMEATTRIBUTE_H__

#include <arc/security/ArcPDP/attr/AttributeValue.h>

namespace ArcSec {
class X500NameAttribute : public AttributeValue {
private:
  static std::string identifier;
  std::string value;
  std::string type;

public:
  X500NameAttribute() : type(identifier){ };
  X500NameAttribute(std::string v) : value(v), type(identifier){ };
  virtual ~X500NameAttribute(){ };

  virtual bool equal(AttributeValue* other);
  virtual std::string encode() {return value;};
  std::string getValue(){ return value; };
  virtual std::string getType() {return type;};
  static const std::string& getIdentifier(void) { return identifier; };
 
};

}// namespace ArcSec

#endif /* __ARC_SEC_X500NAMEATTRIBUTE_H__ */


