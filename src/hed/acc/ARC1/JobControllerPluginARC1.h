// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_JOBCONTROLLERARC1_H__
#define __ARC_JOBCONTROLLERARC1_H__

#include <arc/client/JobControllerPlugin.h>

#include "AREXClient.h"

namespace Arc {

  class JobControllerPluginARC1 : public JobControllerPlugin {
  public:
    JobControllerPluginARC1(const UserConfig& usercfg, PluginArgument* parg) : JobControllerPlugin(usercfg, parg),clients(this->usercfg) { supportedInterfaces.push_back("org.nordugrid.xbes"); }
    ~JobControllerPluginARC1() {}

    static Plugin* Instance(PluginArgument *arg) {
      JobControllerPluginPluginArgument *jcarg = dynamic_cast<JobControllerPluginPluginArgument*>(arg);
      return jcarg ? new JobControllerPluginARC1(*jcarg, arg) : NULL;
    }

    bool isEndpointNotSupported(const std::string& endpoint) const;

    virtual void UpdateJobs(std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const;
    
    virtual bool CleanJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const;
    virtual bool CancelJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const;
    virtual bool RenewJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const;
    virtual bool ResumeJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const;
    
    virtual bool GetURLToJobResource(const Job& job, Job::ResourceType resource, URL& url) const;
    virtual bool GetJobDescription(const Job& job, std::string& desc_str) const;

  private:
    static Logger logger;
    AREXClients clients;
  };

} // namespace Arc

#endif // __ARC_JOBCONTROLLERARC1_H__