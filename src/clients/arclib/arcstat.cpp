// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#include <list>
#include <string>
#include <algorithm>

#include <arc/ArcLocation.h>
#include <arc/IString.h>
#include <arc/Logger.h>
#include <arc/OptionParser.h>
#include <arc/StringConv.h>
#include <arc/client/JobController.h>
#include <arc/client/JobSupervisor.h>
#include <arc/loader/FinderLoader.h>
#include <arc/loader/Plugin.h>
#include <arc/UserConfig.h>

#ifdef TEST
#define RUNSTAT(X) test_arcstat_##X
#else
#define RUNSTAT(X) X
#endif
int RUNSTAT(main)(int argc, char **argv) {

  setlocale(LC_ALL, "");

  Arc::Logger logger(Arc::Logger::getRootLogger(), "arcstat");
  Arc::LogStream logcerr(std::cerr);
  logcerr.setFormat(Arc::ShortFormat);
  Arc::Logger::getRootLogger().addDestination(logcerr);
  Arc::Logger::getRootLogger().setThreshold(Arc::WARNING);

  Arc::ArcLocation::Init(argv[0]);

  Arc::OptionParser options(istring("[job ...]"),
                            istring("The arcstat command is used for "
                                    "obtaining the status of jobs that have\n"
                                    "been submitted to Grid enabled resources."));

  bool all = false;
  options.AddOption('a', "all",
                    istring("all jobs"),
                    all);

  std::string joblist;
  options.AddOption('j', "joblist",
                    istring("the file storing information about active jobs (default ~/.arc/jobs.xml)"),
                    istring("filename"),
                    joblist);

  std::list<std::string> jobidfiles;
  options.AddOption('i', "jobids-from-file",
                    istring("a file containing a list of jobIDs"),
                    istring("filename"),
                    jobidfiles);

  std::list<std::string> clusters;
  options.AddOption('c', "cluster",
                    istring("explicitly select or reject a specific resource"),
                    istring("[-]name"),
                    clusters);

  std::list<std::string> status;
  options.AddOption('s', "status",
                    istring("only select jobs whose status is statusstr"),
                    istring("statusstr"),
                    status);

  bool longlist = false;
  options.AddOption('l', "long",
                    istring("long format (more information)"),
                    longlist);

  // Option 'long' takes precedence over this option (print-jobids).
  bool printids = false;
  options.AddOption('p', "print-jobids", istring("instead of the status only the IDs of "
                    "the selected jobs will be printed"), printids);

  typedef bool (*JobSorting)(const Arc::Job*, const Arc::Job*);
  std::string sort = "", rsort = "";
  std::map<std::string, JobSorting> orderings;
  orderings["jobid"] = &Arc::Job::CompareJobID;
  orderings["submissiontime"] = &Arc::Job::CompareSubmissionTime;
  orderings["jobname"] = &Arc::Job::CompareJobName;
  options.AddOption('S', "sort",
                    istring("sort jobs according to jobid, submissiontime or jobname"),
                    istring("order"), sort);
  options.AddOption('R', "rsort",
                    istring("reverse sorting of jobs according to jobid, submissiontime or jobname"),
                    istring("order"), rsort);

  bool show_plugins = false;
  options.AddOption('P', "listplugins",
                    istring("list the available plugins"),
                    show_plugins);

  int timeout = -1;
  options.AddOption('t', "timeout", istring("timeout in seconds (default 20)"),
                    istring("seconds"), timeout);

  std::string conffile;
  options.AddOption('z', "conffile",
                    istring("configuration file (default ~/.arc/client.conf)"),
                    istring("filename"), conffile);

  std::string debug;
  options.AddOption('d', "debug",
                    istring("FATAL, ERROR, WARNING, INFO, VERBOSE or DEBUG"),
                    istring("debuglevel"), debug);

  bool version = false;
  options.AddOption('v', "version", istring("print version information"),
                    version);

  std::list<std::string> jobidentifiers = options.Parse(argc, argv);

  if (version) {
    std::cout << Arc::IString("%s version %s", "arcstat", VERSION)
              << std::endl;
    return 0;
  }

  // If debug is specified as argument, it should be set before loading the configuration.
  if (!debug.empty())
    Arc::Logger::getRootLogger().setThreshold(Arc::string_to_level(debug));

  if (show_plugins) {
    std::list<Arc::ModuleDesc> modules;
    Arc::PluginsFactory pf(Arc::BaseConfig().MakeConfig(Arc::Config()).Parent());
    pf.scan(Arc::FinderLoader::GetLibrariesList(), modules);
    Arc::PluginsFactory::FilterByKind("HED:JobController", modules);

    std::cout << Arc::IString("Types of services arcstat is able to manage jobs at:") << std::endl;
    for (std::list<Arc::ModuleDesc>::iterator itMod = modules.begin();
         itMod != modules.end(); itMod++) {
      for (std::list<Arc::PluginDesc>::iterator itPlug = itMod->plugins.begin();
           itPlug != itMod->plugins.end(); itPlug++) {
        std::cout << "  " << itPlug->name << " - " << itPlug->description << std::endl;
      }
    }
    return 0;
  }

  Arc::UserConfig usercfg(conffile, joblist);
  if (!usercfg) {
    logger.msg(Arc::ERROR, "Failed configuration initialization");
    return 1;
  }

  if (debug.empty() && !usercfg.Verbosity().empty())
    Arc::Logger::getRootLogger().setThreshold(Arc::string_to_level(usercfg.Verbosity()));

  for (std::list<std::string>::const_iterator it = jobidfiles.begin(); it != jobidfiles.end(); it++) {
    if (!Arc::Job::ReadJobIDsFromFile(*it, jobidentifiers)) {
      logger.msg(Arc::WARNING, "Cannot read specified jobid file: %s", *it);
    }
  }

  if (timeout > 0)
    usercfg.Timeout(timeout);

  if (!sort.empty() && !rsort.empty()) {
    logger.msg(Arc::ERROR, "The 'sort' and 'rsort' flags cannot be specified at the same time.");
    return 1;
  }

  if (!rsort.empty()) {
    sort = rsort;
  }

  if (!sort.empty() && orderings.find(sort) == orderings.end()) {
    std::cerr << "Jobs cannot be sorted by \"" << sort << "\", the following orderings are supported:" << std::endl;
    for (std::map<std::string, JobSorting>::const_iterator it = orderings.begin();
         it != orderings.end(); it++)
      std::cerr << it->first << std::endl;
    return 1;
  }

  if ((!joblist.empty() || !status.empty()) && jobidentifiers.empty() && clusters.empty())
    all = true;

  if (jobidentifiers.empty() && clusters.empty() && !all) {
    logger.msg(Arc::ERROR, "No jobs given");
    return 1;
  }

  if (!jobidentifiers.empty() || all)
    usercfg.ClearSelectedServices();

  if (!clusters.empty()) {
    usercfg.ClearSelectedServices();
    usercfg.AddServices(clusters, Arc::COMPUTING);
  }

  Arc::JobSupervisor jobmaster(usercfg, jobidentifiers);
  if (!jobmaster.JobsFound()) {
    std::cout << Arc::IString("No jobs") << std::endl;
    return 0;
  }
  std::list<Arc::JobController*> jobcont = jobmaster.GetJobControllers();

  if (jobcont.empty()) {
    logger.msg(Arc::ERROR, "No job controller plugins loaded");
    return 1;
  }

  std::vector<const Arc::Job*> jobs;
  for (std::list<Arc::JobController*>::iterator it = jobcont.begin();
       it != jobcont.end(); it++) {
    (*it)->FetchJobs(status, jobs);
  }

  if (!sort.empty()) {
    rsort.empty() ? std::sort(jobs.begin(),  jobs.end(),  orderings[sort]) :
                    std::sort(jobs.rbegin(), jobs.rend(), orderings[sort]);
  }

  for (std::vector<const Arc::Job*>::const_iterator it = jobs.begin();
       it != jobs.end(); it++) {
    // Option 'long' (longlist) takes precedence over option 'print-jobids' (printids)
    if (longlist || !printids) {
      (*it)->SaveToStream(std::cout, longlist);
    }
    else {
      std::cout << (*it)->JobID.fullstr() << std::endl;
    }
  }

  return 0;
}
