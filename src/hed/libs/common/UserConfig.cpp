// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glibmm.h>
#ifdef HAVE_GIOMM
#include <giomm/file.h>
#endif

#include <arc/ArcLocation.h>
#include <arc/IniConfig.h>
#include <arc/Logger.h>
#include <arc/StringConv.h>
#include <arc/URL.h>
#include <arc/User.h>
#include <arc/UserConfig.h>
#include <arc/Utils.h>

#ifdef WIN32 
#include <arc/win32.h>
#endif

namespace Arc {

  Logger UserConfig::logger(Logger::getRootLogger(), "UserConfig");

  typedef std::vector<std::string> strv_t;

  std::list<std::string> UserConfig::resolvedAlias;

  const std::string UserConfig::DEFAULT_BROKER = "Random";
  
  const std::string UserConfig::ARCUSERDIR = Glib::build_filename(User().Home(), ".arc");
  
  UserConfig::UserConfig(bool initializeCredentials) {
    if (initializeCredentials) {
      InitializeCredentials();
      if (!CredentialsFound())
        return;
    }

    ok = true;

    setDefaults();
  }

  UserConfig::UserConfig(const XMLNode& ccfg) {
    // TODO: Probably internal variables should be initilized here.

    ccfg.New(cfg);
    ok = true;
  }

  UserConfig::UserConfig(const std::string& file, bool initializeCredentials)
    : conffile(file),
      userSpecifiedJobList(false),
      ok(false) {
    if (!loadUserConfiguration())
      return;

    if (initializeCredentials) {
      InitializeCredentials();
      if (!CredentialsFound())
        return;
    }

    ok = true;

    setDefaults();
  }

  UserConfig::UserConfig(const std::string& file, const std::string& jfile, bool initializeCredentials)
    : conffile(file),
      joblistfile(jfile),
      userSpecifiedJobList(!jfile.empty()),
      ok(false) {
    if (!loadUserConfiguration())
      return;

    // First check if job list file was given as an argument, next look for it
    // in the user configuration, and last set job list file to default.
    if (joblistfile.empty() &&
        (joblistfile = (std::string)cfg["JobListFile"]).empty())
      joblistfile = Glib::build_filename(ARCUSERDIR, "jobs.xml");

    // Check if joblistfile exist.
    if (!Glib::file_test(joblistfile.c_str(), Glib::FILE_TEST_EXISTS)) {
      // The joblistfile does not exist. Create a empty version, and if
      // this fails report an error and exit.
      const std::string joblistdir = Glib::path_get_dirname(joblistfile);

      // Check if the parent directory exist.
      if (!Glib::file_test(joblistdir, Glib::FILE_TEST_EXISTS)) {
        // Create directory.
        if (!makeDir(joblistdir)) {
          logger.msg(ERROR, "Unable to create %s directory", joblistdir);
          return;
        }
      }
      else if (!Glib::file_test(joblistdir, Glib::FILE_TEST_IS_DIR)) {
        logger.msg(ERROR, "%s is not a directory, it is needed for the client to function correctly", joblistdir);
        return;
      }

      NS ns;
      Config(ns).SaveToFile(joblistfile);
      logger.msg(INFO, "Created empty ARC job list file: %s", joblistfile);
    }
    else if (!Glib::file_test(joblistfile.c_str(), Glib::FILE_TEST_IS_REGULAR)) {
      logger.msg(ERROR, "ARC job list file is not a regular file: %s", joblistfile);
      return;
    }


    if (initializeCredentials) {
      InitializeCredentials();
      if (!CredentialsFound())
        return;
    }

    ok = true;

    setDefaults();
  }

  UserConfig::UserConfig(const long int& ptraddr) { *this = *((UserConfig*)ptraddr); }

  void UserConfig::ApplyToConfig(XMLNode& ccfg) const {
    if (!proxyPath.empty())
      ccfg.NewChild("ProxyPath") = proxyPath;
    else {
      ccfg.NewChild("CertificatePath") = certificatePath;
      ccfg.NewChild("KeyPath") = keyPath;
    }

    ccfg.NewChild("CACertificatesDir") = caCertificatesDir;

    ccfg.NewChild("TimeOut") = (std::string)cfg["TimeOut"];

    if (cfg["Broker"]["Name"]) {
      ccfg.NewChild("Broker").NewChild("Name") = (std::string)cfg["Broker"]["Name"];
      if (cfg["Broker"]["Arguments"])
        ccfg["Broker"].NewChild("Arguments") = (std::string)cfg["Broker"]["Arguments"];
    }
  }

  void UserConfig::ApplyToConfig(BaseConfig& ccfg) const {
    if (!proxyPath.empty())
      ccfg.AddProxy(proxyPath);
    else {
      ccfg.AddCertificate(certificatePath);
      ccfg.AddPrivateKey(keyPath);
    }

    ccfg.AddCADir(caCertificatesDir);
  }

  void UserConfig::SetTimeOut(unsigned int timeOut) {
    if (!cfg["TimeOut"])
      cfg.NewChild("TimeOut");

    cfg["TimeOut"] = tostring(timeOut);
  }

  void UserConfig::SetBroker(const std::string& broker) {
    if (!cfg["Broker"])
      cfg.NewChild("Broker");
    if (!cfg["Broker"]["Name"])
      cfg["Broker"].NewChild("Name");

    const std::string::size_type pos = broker.find(":");

    cfg["Broker"]["Name"] = broker.substr(0, pos);

    if (pos != std::string::npos && pos != broker.size() - 1) {
      if (!cfg["Broker"]["Arguments"])
        cfg["Broker"].NewChild("Arguments");
      cfg["Broker"]["Arguments"] = broker.substr(pos + 1);
    }
    else if (cfg["Broker"]["Arguments"])
      cfg["Broker"]["Arguments"] = "";
  }

  bool UserConfig::DefaultServices(URLListMap& cluster,
                                   URLListMap& index) const {

    logger.msg(INFO, "Finding default services");

    XMLNode defservice = cfg["DefaultServices"];

    for (XMLNode node = defservice["URL"]; node; ++node) {
      std::string flavour = (std::string)node.Attribute("Flavour");
      if (flavour.empty()) {
        logger.msg(ERROR, "URL entry in default services has no "
                   "\"Flavour\" attribute");
        return false;
      }
      std::string serviceType = (std::string)node.Attribute("ServiceType");
      if (serviceType.empty()) {
        logger.msg(ERROR, "URL entry in default services has no "
                   "\"ServiceType\" attribute");
        return false;
      }
      std::string urlstr = (std::string)node;
      if (urlstr.empty()) {
        logger.msg(ERROR, "URL entry in default services is empty");
        return false;
      }
      URL url(urlstr);
      if (!url) {
        logger.msg(ERROR, "URL entry in default services is not a "
                   "valid URL: %s", urlstr);
        return false;
      }
      if (serviceType == "computing") {
        logger.msg(INFO, "Default services contain a computing service of "
                   "flavour %s: %s", flavour, url.str());
        cluster[flavour].push_back(url);
      }
      else if (serviceType == "index") {
        logger.msg(INFO, "Default services contain an index service of "
                   "flavour %s: %s", flavour, url.str());
        index[flavour].push_back(url);
      }
      else {
        logger.msg(ERROR, "URL entry in default services contains "
                   "unknown ServiceType: %s", serviceType);
        return false;
      }
    }

    for (XMLNode node = defservice["Alias"]; node; ++node) {
      std::string aliasstr = (std::string)node;
      if (aliasstr.empty()) {
        logger.msg(ERROR, "Alias entry in default services is empty");
        return false;
      }
      if (!ResolveAlias(aliasstr, cluster, index))
        return false;
    }

    logger.msg(INFO, "Done finding default services");

    return true;
  }

  bool UserConfig::ResolveAlias(const std::string alias,
                                URLListMap& cluster,
                                URLListMap& index) const {

    logger.msg(INFO, "Resolving alias: %s", alias);

    for (std::list<std::string>::iterator it = resolvedAlias.begin();
         it != resolvedAlias.end(); it++)
      if (*it == alias) {
        std::string loopstr = "";
        for (std::list<std::string>::iterator itloop = resolvedAlias.begin();
             itloop != resolvedAlias.end(); itloop++)
          loopstr += *itloop + " -> ";
        loopstr += alias;
        logger.msg(ERROR, "Cannot resolve alias \"%s\". Loop detected: %s", *resolvedAlias.begin(), alias);

        resolvedAlias.clear();
        return false;
      }

    XMLNodeList aliaslist = cfg.XPathLookup("//AliasList/Alias[@name='" +
                                            alias + "']", NS());

    if (aliaslist.empty()) {
      logger.msg(ERROR, "Alias \"%s\" requested but not defined", alias);
      resolvedAlias.clear();
      return false;
    }

    XMLNode& aliasnode = *aliaslist.begin();

    for (XMLNode node = aliasnode["URL"]; node; ++node) {
      std::string flavour = (std::string)node.Attribute("Flavour");
      if (flavour.empty()) {
        logger.msg(ERROR, "URL entry in alias definition \"%s\" has no "
                   "\"Flavour\" attribute", alias);
        resolvedAlias.clear();
        return false;
      }
      std::string serviceType = (std::string)node.Attribute("ServiceType");
      if (flavour.empty()) {
        logger.msg(ERROR, "URL entry in alias definition \"%s\" has no "
                   "\"ServiceType\" attribute", alias);
        resolvedAlias.clear();
        return false;
      }
      std::string urlstr = (std::string)node;
      if (urlstr.empty()) {
        logger.msg(ERROR, "URL entry in alias definition \"%s\" is empty", alias);
        resolvedAlias.clear();
        return false;
      }
      URL url(urlstr);
      if (!url) {
        logger.msg(ERROR, "URL entry in alias definition \"%s\" is not a "
                   "valid URL: %s", alias, urlstr);
        resolvedAlias.clear();
        return false;
      }
      if (serviceType == "computing") {
        logger.msg(INFO, "Alias %s contains a computing service of "
                   "flavour %s: %s", alias, flavour, url.str());
        cluster[flavour].push_back(url);
      }
      else if (serviceType == "index") {
        logger.msg(INFO, "Alias %s contains an index service of "
                   "flavour %s: %s", alias, flavour, url.str());
        index[flavour].push_back(url);
      }
      else {
        logger.msg(ERROR, "URL entry in alias definition \"%s\" contains "
                   "unknown ServiceType: %s", alias, serviceType);
        resolvedAlias.clear();
        return false;
      }
    }

    resolvedAlias.push_back(alias);

    for (XMLNode node = aliasnode["AliasRef"]; node; ++node) {
      std::string aliasstr = (std::string)node;
      if (aliasstr.empty()) {
        logger.msg(ERROR, "Alias entry in alias definition \"%s\" is empty", alias);
        resolvedAlias.clear();
        return false;
      }
      if (!ResolveAlias(aliasstr, cluster, index))
        return false;
    }
    logger.msg(INFO, "Done resolving alias: %s", alias);
    resolvedAlias.clear();
    return true;
  }

  bool UserConfig::ResolveAlias(const std::list<std::string>& clusters,
                                const std::list<std::string>& indices,
                                URLListMap& clusterselect,
                                URLListMap& clusterreject,
                                URLListMap& indexselect,
                                URLListMap& indexreject) const {

    for (std::list<std::string>::const_iterator it = clusters.begin();
         it != clusters.end(); it++) {
      bool select = true;
      std::string cluster = *it;
      if (cluster[0] == '-')
        select = false;
      if ((cluster[0] == '-') || (cluster[0] == '+'))
        cluster = cluster.substr(1);

      std::string::size_type colon = cluster.find(':');
      if (colon == std::string::npos) {
        if (select) {
          if (!ResolveAlias(cluster, clusterselect, indexselect))
            return false;
        }
        else
          if (!ResolveAlias(cluster, clusterreject, indexreject))
            return false;
      }
      else {
        std::string flavour = cluster.substr(0, colon);
        std::string urlstr = cluster.substr(colon + 1);
        URL url(urlstr);
        if (!url) {
          logger.msg(ERROR, "%s is not a valid URL", urlstr);
          return false;
        }
        if (select)
          clusterselect[flavour].push_back(url);
        else
          clusterreject[flavour].push_back(url);
      }
    }

    for (std::list<std::string>::const_iterator it = indices.begin();
         it != indices.end(); it++) {
      bool select = true;
      std::string index = *it;
      if (index[0] == '-')
        select = false;
      if ((index[0] == '-') || (index[0] == '+'))
        index = index.substr(1);

      std::string::size_type colon = index.find(':');
      if (colon == std::string::npos) {
        if (select) {
          if (!ResolveAlias(index, clusterselect, indexselect))
            return false;
        }
        else
          if (!ResolveAlias(index, clusterreject, indexreject))
            return false;
      }
      else {
        std::string flavour = index.substr(0, colon);
        std::string urlstr = index.substr(colon + 1);
        URL url(urlstr);
        if (!url) {
          logger.msg(ERROR, "%s is not a valid URL", urlstr);
          return false;
        }
        if (select)
          indexselect[flavour].push_back(url);
        else
          indexreject[flavour].push_back(url);
      }
    }
    return true;
  }

  bool UserConfig::ResolveAlias(const std::list<std::string>& clusters,
                                URLListMap& clusterselect,
                                URLListMap& clusterreject) const {

    std::list<std::string> indices;
    URLListMap indexselect;
    URLListMap indexreject;
    return ResolveAlias(clusters, indices, clusterselect,
                        clusterreject, indexselect, indexreject);
  }

  void UserConfig::InitializeCredentials() {
    struct stat st;
    // Look for credentials.
    if (!(proxyPath = GetEnv("X509_USER_PROXY")).empty()) {
      if (stat(proxyPath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access proxy file: %s (%s)",
                   proxyPath, StrError());
        proxyPath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Proxy is not a file: %s", proxyPath);
        proxyPath.clear();
      }
    }
    else if (!(certificatePath = GetEnv("X509_USER_CERT")).empty() &&
             !(keyPath = GetEnv("X509_USER_KEY")).empty()) {
      if (stat(certificatePath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access certificate file: %s (%s)",
                   certificatePath, StrError());
        certificatePath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Certificate is not a file: %s", certificatePath);
        certificatePath.clear();
      }

      if (stat(keyPath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access key file: %s (%s)",
                   keyPath, StrError());
        keyPath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Key is not a file: %s", keyPath);
        keyPath.clear();
      }
    }
    else if (!(proxyPath = (std::string)cfg["ProxyPath"]).empty()) {
      if (stat(proxyPath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access proxy file: %s (%s)",
                   proxyPath, StrError());
        proxyPath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Proxy is not a file: %s", proxyPath);
        proxyPath.clear();
      }
    }
    else if (!(certificatePath = (std::string)cfg["CertificatePath"]).empty() &&
             !(keyPath = (std::string)cfg["KeyPath"]).empty()) {
      if (stat(certificatePath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access certificate file: %s (%s)",
                   certificatePath, StrError());
        certificatePath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Certificate is not a file: %s", certificatePath);
        certificatePath.clear();
      }

      if (stat(keyPath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access key file: %s (%s)",
                   keyPath, StrError());
        keyPath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Key is not a file: %s", keyPath);
        keyPath.clear();
      }
    }
    else if (stat((proxyPath = Glib::build_filename(Glib::get_tmp_dir(), std::string("x509up_u") + tostring(user.get_uid()))).c_str(), &st) == 0) {
      if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Proxy is not a file: %s", proxyPath);
        proxyPath.clear();
      }
    }
    else {
      logger.msg(WARNING, "Default proxy file does not exist: %s "
                 "trying default certificate and key", proxyPath);
      if (user.get_uid() == 0) {
        strv_t hostcert(3);
        hostcert[0] = "etc";
        hostcert[1] = "grid-security";
        hostcert[2] = "hostcert.pem";
        certificatePath = G_DIR_SEPARATOR_S + Glib::build_filename(hostcert);
      }
      else {
        strv_t usercert(3);
        usercert[0] = user.Home();
        usercert[1] = ".globus";
        usercert[2] = "usercert.pem";
        certificatePath = Glib::build_filename(usercert);
      }

      if (stat(certificatePath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access certificate file: %s (%s)", certificatePath, StrError());
        certificatePath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Certificate is not a file: %s", certificatePath);
        certificatePath.clear();
      }

      if (user.get_uid() == 0) {
        strv_t hostkey(3);
        hostkey[0] = "etc";
        hostkey[1] = "grid-security";
        hostkey[2] = "hostkey.pem";
        keyPath = G_DIR_SEPARATOR_S + Glib::build_filename(hostkey);
      }
      else {
        strv_t userkey(3);
        userkey[0] = user.Home();
        userkey[1] = ".globus";
        userkey[2] = "userkey.pem";
        keyPath = Glib::build_filename(userkey);
      }

      if (stat(keyPath.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access key file: %s (%s)",
                   keyPath, StrError());
        keyPath.clear();
      }
      else if (!S_ISREG(st.st_mode)) {
        logger.msg(ERROR, "Key is not a file: %s", keyPath);
        keyPath.clear();
      }
    }

    if (!(caCertificatesDir = GetEnv("X509_CERT_DIR")).empty()) {
      if (stat(caCertificatesDir.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access CA certificate directory: %s (%s)",
                   caCertificatesDir, StrError());
        caCertificatesDir.clear();
      }
      else if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (!(caCertificatesDir = (std::string)cfg["CACertificatesDir"]).empty()) {
      if (stat(caCertificatesDir.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access CA certificate directory: %s (%s)",
                   caCertificatesDir, StrError());
        caCertificatesDir.clear();
      }
      else if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (user.get_uid() != 0 &&
             stat((caCertificatesDir = user.Home() + G_DIR_SEPARATOR_S + ".globus" + G_DIR_SEPARATOR_S + "certificates").c_str(), &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (stat((caCertificatesDir = std::string(g_get_home_dir()) + G_DIR_SEPARATOR_S + ".globus" + G_DIR_SEPARATOR_S + "certificates").c_str(), &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (stat((caCertificatesDir = Arc::ArcLocation::Get() + G_DIR_SEPARATOR_S + "etc" + G_DIR_SEPARATOR_S + "certificates").c_str(), &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (stat((caCertificatesDir = Arc::ArcLocation::Get() + G_DIR_SEPARATOR_S + "etc" + G_DIR_SEPARATOR_S + "grid-security" + G_DIR_SEPARATOR_S + "certificates").c_str(), &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else if (stat((caCertificatesDir = Arc::ArcLocation::Get() + G_DIR_SEPARATOR_S + "share" + G_DIR_SEPARATOR_S + "certificates").c_str(), &st) == 0) {
      if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }
    else {
      strv_t caCertificatesPath(3);
      caCertificatesPath[0] = "etc";
      caCertificatesPath[1] = "grid-security";
      caCertificatesPath[2] = "certificates";
      caCertificatesDir = G_DIR_SEPARATOR_S + Glib::build_filename(caCertificatesPath);
      if (stat(caCertificatesDir.c_str(), &st) != 0) {
        logger.msg(ERROR, "Can not access CA certificate directory: %s (%s)",
                   caCertificatesDir, StrError());
        caCertificatesDir.clear();
      }
      else if (!S_ISDIR(st.st_mode)) {
        logger.msg(ERROR, "CA certificate directory is not a directory: %s", caCertificatesDir);
        caCertificatesDir.clear();
      }
    }


    if (!proxyPath.empty())
      logger.msg(INFO, "Using proxy file: %s", proxyPath);
    else if (!certificatePath.empty() && !keyPath.empty()) {
      logger.msg(INFO, "Using certificate file: %s", certificatePath);
      logger.msg(INFO, "Using key file: %s", keyPath);

      if (!caCertificatesDir.empty())
        logger.msg(INFO, "Using CA certificate directory: %s", caCertificatesDir);
    }
  }

  bool UserConfig::loadUserConfiguration() {
    const bool isConfigSpecified = !conffile.empty();
    
    // If no user configuration file was specified use the default.
    if (!isConfigSpecified) {
      conffile = Glib::build_filename(ARCUSERDIR, "client.xml");

      // If the default configuration file does not exist, copy an example file.
      if (!Glib::file_test(conffile, Glib::FILE_TEST_EXISTS)) {

        // Check if the parent directory exist.
        if (!Glib::file_test(ARCUSERDIR, Glib::FILE_TEST_EXISTS)) {
          // Create directory.
          if (!makeDir(ARCUSERDIR)) {
            logger.msg(WARNING, "Unable to create %s directory.", ARCUSERDIR);
          }
        }

        if (Glib::file_test(ARCUSERDIR, Glib::FILE_TEST_IS_DIR)) {
          const std::string confexample = Glib::build_filename(PKGDOCDIR, "client.xml.example");
          if (Glib::file_test(confexample, Glib::FILE_TEST_IS_REGULAR)) {
            // Destination: Get basename, remove example prefix and add .arc directory.
            if (copyFile(confexample, conffile))
              logger.msg(INFO, "Configuration example file created (%s)", conffile);
            else
              logger.msg(WARNING, "Unable to copy example configuration from existing configuration (%s)", confexample);
          }
          else {
            logger.msg(WARNING, "Cannot copy example configuration (%s), it is not a regular file", confexample);
          }
        }
        else {
          logger.msg(WARNING, "Example configuration (%s) not created.", conffile);
        }
      }
      else if (!Glib::file_test(conffile, Glib::FILE_TEST_IS_REGULAR)) {
        logger.msg(WARNING, "The default configuration file (%s) is not a regular file.", conffile);
        conffile = "";
      }
    }
    else if (!Glib::file_test(conffile, Glib::FILE_TEST_IS_REGULAR)) {
      logger.msg(ERROR, "The specified configuration file (%s) is not a regular file", conffile);
      return false;
    }

    // First try to load system client configuration.
    const std::string sysconf = Glib::build_filename(PKGSYSCONFDIR, "client.xml");
    if (Glib::file_test(sysconf, Glib::FILE_TEST_IS_REGULAR)) {
      if (cfg.ReadFromFile(sysconf))
        logger.msg(INFO, "System configuration (%s) loaded", sysconf);
      else
        logger.msg(WARNING, "Unable to load system configuration (%s)", sysconf);
    }

    if (!conffile.empty()) {
      Config ucfg;
      if (ucfg.ReadFromFile(conffile)) {
        // Merge system and user configuration
        XMLNode child;
        for (int i = 0; (child = ucfg.Child(i)); i++) {
          if (child.Name() != "AliasList") {
            if (cfg[child.Name()])
              cfg[child.Name()].Replace(child);
            else
              cfg.NewChild(child);
          }
          else {
            if (!cfg["AliasList"])
              cfg.NewChild(child);
            else
              // Look for duplicates. If duplicates exist, keep those defined in
              // the user configuration file.
              for (XMLNode alias = child["Alias"]; alias; ++alias) { // Loop over Alias nodes in user configuration file.
                XMLNodeList aliasList = cfg.XPathLookup("//AliasList/Alias[@name='" + (std::string)alias.Attribute("name") + "']", NS());
                if (!aliasList.empty())
                  // Remove duplicates.
                  for (XMLNodeList::iterator node = aliasList.begin(); node != aliasList.end(); node++)
                    node->Destroy();
                cfg["AliasList"].NewChild(alias);
              }
          }
        }

        logger.msg(INFO, "XML user configuration (%s) loaded", conffile);
      }
      else {
        IniConfig ini(conffile);
        if (ini) {
          XMLNode child;
          for (int i = 0; (child = ini["common"].Child(i)); i++) {
            if (cfg[child.Name()])
              cfg[child.Name()].Replace(child);
            else
              cfg.NewChild(child);
          }

          logger.msg(INFO, "INI user configuration (%s) loaded", conffile);
        }
        else
          logger.msg(WARNING, "Could not load user client configuration");
      }
    }

    return true;
  }

  void UserConfig::setDefaults() {
    if (!cfg["TimeOut"])
      cfg.NewChild("TimeOut") = tostring(DEFAULT_TIMEOUT);
    else if (((std::string)cfg["TimeOut"]).empty() || stringtoi((std::string)cfg["TimeOut"]) <= 0)
      cfg["TimeOut"] = tostring(DEFAULT_TIMEOUT);

    if (!cfg["Broker"]["Name"]) {
      if (!cfg["Broker"])
        cfg.NewChild("Broker");
      cfg["Broker"].NewChild("Name") = DEFAULT_BROKER;
      if (cfg["Broker"]["Arguments"])
        cfg["Broker"]["Arguments"] = "";
    }
  }

  bool UserConfig::makeDir(const std::string& path) {
    bool dirCreated = false;
#ifdef HAVE_GIOMM
    try {
      dirCreated = Gio::File::create_for_path(path)->make_directory();
    } catch (Gio::Error e) {
      logger.msg(WARNING, "%s", (std::string)e.what());
    }
#else
    dirCreated = (mkdir(path.c_str(), 0755) == 0);
#endif

    if (dirCreated)
      logger.msg(INFO, "%s directory created", path);

    return dirCreated;
  }

  bool UserConfig::copyFile(const std::string& source, const std::string& destination) {
#ifdef HAVE_GIOMM
    try {
      return Gio::File::create_for_path(source)->copy(Gio::File::create_for_path(destination), Gio::FILE_COPY_NONE);
    } catch (Gio::Error e) {
      logger.msg(WARNING, "%s", (std::string)e.what());
      return false;
    }
#else
    std::ifstream ifsSource(source.c_str(), std::ios::in | std::ios::binary);
    if (!ifsSource)
      return false;
      
    std::ofstream ofsDestination(destination.c_str(), std::ios::out | std::ios::binary);
    if (!ofsDestination)
      return false;

    int bytesRead = -1;
    char buff[1024];
    while (bytesRead != 0) {
      ifsSource.read((char*)buff, 1024);
      bytesRead = ifsSource.gcount();
      ofsDestination.write((char*)buff, bytesRead);
    }
    
    return true;
#endif
  }

} // namespace Arc
