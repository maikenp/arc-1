#ifndef __ARC_SEC_GACLPOLICY_H__
#define __ARC_SEC_GACLPOLICY_H__

#include <arc/security/ArcPDP/policy/Policy.h>

namespace ArcSec {

class GACLPolicy : public Policy {
public:
  GACLPolicy(Arc::PluginArgument* parg);

  GACLPolicy(const Source& source, Arc::PluginArgument* parg);

  GACLPolicy(const Arc::XMLNode source, Arc::PluginArgument* parg);

  virtual ~GACLPolicy();  

  virtual operator bool(void) const { return (bool)policynode; };

  virtual Result eval(EvaluationCtx* ctx);

  virtual MatchResult match(EvaluationCtx* /* ctx */) { return NO_MATCH; };

  virtual std::string getEffect() const { return ""; };

  virtual EvalResult& getEvalResult();

  virtual void setEvalResult(EvalResult& res);

  Arc::XMLNode getXML(void) { return policynode; };

  virtual const char* getEvalName() const { return "gacl.evaluator"; };

  virtual const char* getName() const { return "gacl.policy"; };

  static Arc::Plugin* get_policy(Arc::PluginArgument* arg);

private:

  EvalResult evalres;

  Arc::XMLNode policynode;

protected:
  static Arc::Logger logger;
};

} // namespace ArcSec

#endif /* __ARC_SEC_GACLPOLICY_H__ */

