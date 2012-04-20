// -*- indent-tabs-mode: nil -*-

#ifndef __ARC_JOBCONTROLLER_H__
#define __ARC_JOBCONTROLLER_H__

#include <list>
#include <vector>
#include <string>

#include <arc/URL.h>
#include <arc/client/Job.h>
#include <arc/loader/Loader.h>
#include <arc/loader/Plugin.h>

namespace Arc {

  class Broker;
  class Logger;
  class UserConfig;

  // Must be specialiced for each supported middleware flavour.
  class JobController
    : public Plugin {
  protected:
    JobController(const UserConfig& usercfg, PluginArgument* parg)
      : Plugin(parg), usercfg(usercfg) {}
  public:
    virtual ~JobController() {}

    virtual void UpdateJobs(std::list<Job*>& jobs, bool isGrouped = false) const;
    virtual void UpdateJobs(std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const = 0;

    virtual bool CleanJobs(const std::list<Job*>& jobs, bool isGrouped = false) const;
    virtual bool CleanJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const = 0;
    virtual bool CancelJobs(const std::list<Job*>& jobs, bool isGrouped = false) const;
    virtual bool CancelJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const = 0;
    virtual bool RenewJobs(const std::list<Job*>& jobs, bool isGrouped = false) const;
    virtual bool RenewJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const = 0;
    virtual bool ResumeJobs(const std::list<Job*>& jobs, bool isGrouped = false) const;
    virtual bool ResumeJobs(const std::list<Job*>& jobs, std::list<URL>& IDsProcessed, std::list<URL>& IDsNotProcessed, bool isGrouped = false) const = 0;

    virtual bool GetJobDescription(const Job& job, std::string& desc_str) const = 0;
    virtual bool GetURLToJobResource(const Job& job, Job::ResourceType resource, URL& url) const = 0;

    virtual std::string GetGroupID() const { return ""; }
    
    virtual const std::list<std::string>& SupportedInterfaces() const { return supportedInterfaces; };

  protected:
    const UserConfig& usercfg;
    std::list<std::string> supportedInterfaces;
    static Logger logger;
  };

  //! Class responsible for loading JobController plugins
  /// The JobController objects returned by a JobControllerLoader
  /// must not be used after the JobControllerLoader goes out of scope.
  class JobControllerLoader
    : public Loader {

  public:
    //! Constructor
    /// Creates a new JobControllerLoader.
    JobControllerLoader();

    //! Destructor
    /// Calling the destructor destroys all JobControllers loaded
    /// by the JobControllerLoader instance.
    ~JobControllerLoader();

    //! Load a new JobController
    /// \param name    The name of the JobController to load.
    /// \param usercfg The UserConfig object for the new JobController.
    /// \returns       A pointer to the new JobController (NULL on error).
    JobController* load(const std::string& name, const UserConfig& uc);

    JobController* loadByInterfaceName(const std::string& name, const UserConfig& uc);

  private:
    void initialiseInterfacePluginMap(const UserConfig& uc);
  
    std::multimap<std::string, JobController*> jobcontrollers;
    static std::map<std::string, std::string> interfacePluginMap;
  };

  class JobControllerPluginArgument : public PluginArgument {
  public:
    JobControllerPluginArgument(const UserConfig& usercfg) : usercfg(usercfg) {}
    ~JobControllerPluginArgument() {}
    operator const UserConfig&() { return usercfg; }
  private:
    const UserConfig& usercfg;
  };

} // namespace Arc

#endif // __ARC_JOBCONTROLLER_H__
