// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <arc/ArcConfig.h>
#include <arc/IString.h>
#include <arc/credential/Credential.h>
#include <arc/loader/FinderLoader.h>
#include <arc/loader/Plugin.h>

#include "utils.h"

std::list<Arc::ServiceEndpoint> getServicesFromUserConfigAndCommandLine(Arc::UserConfig usercfg, std::list<std::string> registries, std::list<std::string> ces) {
  std::list<Arc::ServiceEndpoint> services;
  if (ces.empty() && registries.empty()) {
    std::list<Arc::ConfigEndpoint> endpoints = usercfg.GetDefaultServices();
    for (std::list<Arc::ConfigEndpoint>::const_iterator its = endpoints.begin(); its != endpoints.end(); its++) {
      services.push_back(Arc::ServiceEndpoint(*its));
    }   
  } else {
    for (std::list<std::string>::const_iterator it = ces.begin(); it != ces.end(); it++) {
      const std::string& ce = *it;
      // check if the string is a name of a group
      std::list<Arc::ConfigEndpoint> servicesInGroup = usercfg.ServicesInGroup(ce, Arc::ConfigEndpoint::COMPUTINGINFO);
      if (servicesInGroup.empty()) {
        // if it's not the name of a group, maybe it's an alias
        Arc::ServiceEndpoint service(usercfg.ResolveService(ce));
        if (service.URLString.empty()) {
          // if it was not an alias, then it should be the URL
          service.URLString = ce;
          service.Capability.push_back(Arc::ComputingInfoEndpoint::ComputingInfoCapability);
        }
        services.push_back(service);        
      } else {
        // if it was a name of a group, add all the services from the group
        for (std::list<Arc::ConfigEndpoint>::const_iterator its = servicesInGroup.begin(); its != servicesInGroup.end(); its++) {
          services.push_back(Arc::ServiceEndpoint(*its));
        }   
      }
    }    
    for (std::list<std::string>::const_iterator it = registries.begin(); it != registries.end(); it++) {
      const std::string& registry = *it;
      // check if the string is a name of a group
      std::list<Arc::ConfigEndpoint> servicesInGroup = usercfg.ServicesInGroup(registry, Arc::ConfigEndpoint::REGISTRY);
      if (servicesInGroup.empty()) {
        // if it's not the name of a group, maybe it's an alias
        Arc::ServiceEndpoint service(usercfg.ResolveService(registry));
        if (service.URLString.empty()) {
          // if it was not an alias, then it should be the URL
          service.URLString = registry;
          service.Capability.push_back(Arc::RegistryEndpoint::RegistryCapability);
        }
        services.push_back(service);        
      } else {
        // if it was a name of a group, add all the services from the group
        for (std::list<Arc::ConfigEndpoint>::const_iterator its = servicesInGroup.begin(); its != servicesInGroup.end(); its++) {
         services.push_back(Arc::ServiceEndpoint(*its));
        }   
      }
    }    
  }
  return services;
}


void showplugins(const std::string& program, const std::list<std::string>& types, Arc::Logger& logger, const std::string& chosenBroker) {

  for (std::list<std::string>::const_iterator itType = types.begin();
       itType != types.end(); ++itType) {
    if (*itType == "HED:Submitter") {
      std::cout << Arc::IString("Types of execution services %s is able to submit jobs to:", program) << std::endl;
    }
    else if (*itType == "HED:TargetRetriever") {
      std::cout << Arc::IString("Types of index and information services which %s is able collect information from:", program) << std::endl;
    }
    else if (*itType == "HED:JobController") {
      std::cout << Arc::IString("Types of services %s is able to manage jobs at:", program) << std::endl;
    }
    else if (*itType == "HED:JobDescriptionParser") {
      std::cout << Arc::IString("Job description languages supported by %s:", program) << std::endl;
    }
    else if (*itType == "HED:Broker") {
      std::cout << Arc::IString("Brokers available to %s:", program) << std::endl;
    }

    std::list<Arc::ModuleDesc> modules;
    Arc::PluginsFactory pf(Arc::BaseConfig().MakeConfig(Arc::Config()).Parent());

    bool isDefaultBrokerLocated = false;
    pf.scan(Arc::FinderLoader::GetLibrariesList(), modules);
    Arc::PluginsFactory::FilterByKind(*itType, modules);
    for (std::list<Arc::ModuleDesc>::iterator itMod = modules.begin();
         itMod != modules.end(); itMod++) {
      for (std::list<Arc::PluginDesc>::iterator itPlug = itMod->plugins.begin();
           itPlug != itMod->plugins.end(); itPlug++) {
        std::cout << "  " << itPlug->name;
        if (*itType == "HED:Broker" && itPlug->name == chosenBroker) {
          std::cout << " (default)";
          isDefaultBrokerLocated = true;
        }
        std::cout << " - " << itPlug->description << std::endl;
      }
    }

    if (*itType == "HED:Broker" && !isDefaultBrokerLocated) {
      logger.msg(Arc::WARNING, "Default broker (%s) is not available. When using %s a broker should be specified explicitly (-b option).", chosenBroker, program);
    }
  }
}

bool checkproxy(const Arc::UserConfig& uc)
{
  if (!uc.ProxyPath().empty() ) {
    Arc::Credential holder(uc.ProxyPath(), "", "", "");
    if (holder.GetEndTime() < Arc::Time()){
      std::cout << Arc::IString("Proxy expired. Job submission aborted. Please run 'arcproxy'!") << std::endl;
      return false;
    }
  }
  else {
    std::cout << Arc::IString("Cannot find any proxy. arcresub currently cannot run without a proxy.\n"
                              "  If you have the proxy file in a non-default location,\n"
                              "  please make sure the path is specified in the client configuration file.\n"
                              "  If you don't have a proxy yet, please run 'arcproxy'!") << std::endl;
    return false;
  }

  return true;
}

void splitendpoints(std::list<std::string>& selected, std::list<std::string>& rejected)
{
  // Removes slashes from end of endpoint strings, and put strings with leading '-' into rejected list.
  for (std::list<std::string>::iterator it = selected.begin();
       it != selected.end();) {
    if ((*it)[it->length()-1] == '/') {
      it->erase(it->length()-1);
      continue;
    }
    if (it->empty()) {
      it = selected.erase(it);
      continue;
    }

    if ((*it)[0] == '-') {
      rejected.push_back(it->substr(1));
      it = selected.erase(it);
    }
    else {
      ++it;
    }
  }
}

ClientOptions::ClientOptions(Client_t c,
                             const std::string& arguments,
                             const std::string& summary,
                             const std::string& description) :
    Arc::OptionParser(arguments, summary, description),
    dryrun(false),
    dumpdescription(false),
    show_credentials(false),
    show_plugins(false),
    showversion(false),
    all(false),
    forcemigration(false),
    keep(false),
    forcesync(false),
    truncate(false),
    longlist(false),
    printids(false),
    same(false),
    notsame(false),
    show_stdout(true),
    show_stderr(false),
    show_joblog(false),
    usejobname(false),
    forcedownload(false),
    testjobid(-1),
    timeout(-1)
{
  bool cIsJobMan = (c == CO_CAT || c == CO_CLEAN || c == CO_GET || c == CO_KILL || c == CO_RENEW || c == CO_RESUME || c == CO_STAT || c == CO_ACL);

  AddOption('c', "cluster",
            istring("explicitly select or reject a specific resource"),
            istring("[-]name"),
            clusters);

  if (c == CO_RESUB || c == CO_MIGRATE) {
    AddOption('q', "qluster",
              istring("explicitly select or reject a specific resource "
                      "for new jobs"),
              istring("[-]name"),
              qlusters);
  }

  if (c == CO_MIGRATE) {
    AddOption('f', "force",
              istring("force migration, ignore kill failure"),
              forcemigration);
  }

  if (c == CO_GET || c == CO_KILL || c == CO_MIGRATE || c == CO_RESUB) {
    AddOption('k', "keep",
              istring("keep the files on the server (do not clean)"),
              keep);
  }

  if (c == CO_SYNC) {
    AddOption('f', "force",
              istring("do not ask for verification"),
              forcesync);

    AddOption('T', "truncate",
              istring("truncate the joblist before synchronizing"),
              truncate);
  }

  if (c == CO_INFO || c == CO_STAT) {
    AddOption('l', "long",
              istring("long format (more information)"),
              longlist);
  }

  if (c == CO_CAT) {
    AddOption('o', "stdout",
              istring("show the stdout of the job (default)"),
              show_stdout);

    AddOption('e', "stderr",
              istring("show the stderr of the job"),
              show_stderr);

    AddOption('l', "joblog",
              istring("show the CE's error log of the job"),
              show_joblog);
  }

  if (c == CO_GET) {
    AddOption('D', "dir",
              istring("download directory (the job directory will"
                      " be created in this directory)"),
              istring("dirname"),
              downloaddir);

    AddOption('J', "usejobname",
              istring("use the jobname instead of the short ID as"
                      " the job directory name"),
              usejobname);

    AddOption('f', "force",
              istring("force download (overwrite existing job directory)"),
              forcedownload);
  }

  if (c == CO_STAT) {
    // Option 'long' takes precedence over this option (print-jobids).
    AddOption('p', "print-jobids", istring("instead of the status only the IDs of "
              "the selected jobs will be printed"), printids);

    AddOption('S', "sort",
              istring("sort jobs according to jobid, submissiontime or jobname"),
              istring("order"), sort);
    AddOption('R', "rsort",
              istring("reverse sorting of jobs according to jobid, submissiontime or jobname"),
              istring("order"), rsort);
  }

  if (c == CO_RESUB) {
    AddOption('m', "same",
              istring("resubmit to the same resource"),
              same);

    AddOption('M', "not-same",
              istring("do not resubmit to the same resource"),
              notsame);
  }

  if (c == CO_CLEAN) {
    AddOption('f', "force",
              istring("remove the job from the local list of jobs "
                      "even if the job is not found in the infosys"),
              forceclean);
  }

  if (!cIsJobMan) {
    AddOption('g', "index",
              istring("explicitly select or reject an index server"),
              istring("[-]name"),
              indexurls);
  }

  if (c == CO_TEST) {
    AddOption('J', "job",
              istring("submit test job given by the number"),
              istring("int"),
              testjobid);
  }

  if (cIsJobMan || c == CO_RESUB) {
    AddOption('s', "status",
              istring("only select jobs whose status is statusstr"),
              istring("statusstr"),
              status);
  }

  if (cIsJobMan || c == CO_MIGRATE || c == CO_RESUB) {
    AddOption('a', "all",
              istring("all jobs"),
              all);
  }

  if (c == CO_SUB) {
    AddOption('e', "jobdescrstring",
              istring("jobdescription string describing the job to "
                      "be submitted"),
              istring("string"),
              jobdescriptionstrings);

    AddOption('f', "jobdescrfile",
              istring("jobdescription file describing the job to "
                      "be submitted"),
              istring("string"),
              jobdescriptionfiles);
  }

  if (c == CO_MIGRATE || c == CO_RESUB || c == CO_SUB || c == CO_TEST) {
    AddOption('b', "broker",
              istring("select broker method (list available brokers with --listplugins flag)"),
              istring("broker"), broker);

    AddOption('o', "jobids-to-file",
              istring("the IDs of the submitted jobs will be appended to this file"),
              istring("filename"),
              jobidoutfile);
  }

  if (cIsJobMan || c == CO_MIGRATE || c == CO_RESUB) {
    AddOption('i', "jobids-from-file",
              istring("a file containing a list of jobIDs"),
              istring("filename"),
              jobidinfiles);
  }

  if (c == CO_SUB || c == CO_TEST) {
    AddOption('D', "dryrun", istring("submit jobs as dry run (no submission to batch system)"),
              dryrun);

    AddOption('x', "dumpdescription",
              istring("do not submit - dump job description "
                      "in the language accepted by the target"),
              dumpdescription);
  }

  if (c == CO_TEST) {
    AddOption('E', "certificate", istring("prints info about installed user- and CA-certificates"), show_credentials);
  }

  if (c != CO_INFO) {
    AddOption('j', "joblist",
              istring("the file storing information about active jobs (default ~/.arc/jobs.xml)"),
              istring("filename"),
              joblist);
  }

  /* --- Standard options below --- */

  AddOption('z', "conffile",
            istring("configuration file (default ~/.arc/client.conf)"),
            istring("filename"), conffile);

  AddOption('t', "timeout", istring("timeout in seconds (default 20)"),
            istring("seconds"), timeout);

  AddOption('P', "listplugins",
            istring("list the available plugins"),
            show_plugins);

  AddOption('d', "debug",
            istring("FATAL, ERROR, WARNING, INFO, VERBOSE or DEBUG"),
            istring("debuglevel"), debug);

  AddOption('v', "version", istring("print version information"),
            showversion);

}

