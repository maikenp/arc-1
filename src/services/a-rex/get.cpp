#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include <string>

#include <glibmm.h>

#include <arc/StringConv.h>
#include <arc/message/PayloadRaw.h>
#include "PayloadFile.h"
#include "job.h"

#include "arex.h"

#define MAX_CHUNK_SIZE (10*1024*1024)

namespace ARex {

static Arc::MCC_Status http_get(Arc::Message& outmsg,const std::string& burl,const std::string& bpath,std::string hpath,size_t start,size_t end);

Arc::MCC_Status ARexService::Get(Arc::Message& inmsg,Arc::Message& outmsg,ARexGMConfig& config,const std::string& id,const std::string& subpath) {
  size_t range_start = 0;
  size_t range_end = (size_t)(-1);
  {
    std::string val;
    val=inmsg.Attributes()->get("HTTP:RANGESTART");
    if(!val.empty()) { // Negative ranges not supported
      if(!Arc::stringto<size_t>(val,range_start)) {
        range_start=0;
      } else {
        val=inmsg.Attributes()->get("HTTP:RANGEEND");
        if(!val.empty()) {
          if(!Arc::stringto<size_t>(val,range_end)) {
            range_end=(size_t)(-1);
          };
        };
      };
    };
  };
  if(id.empty()) {
    // Make list of jobs
    std::string html;
    html="<HTML>\r\n<HEAD>\r\n<TITLE>ARex: Jobs list</TITLE>\r\n</HEAD>\r\n<BODY>\r\n<UL>\r\n";
    std::list<std::string> jobs = ARexJob::Jobs(config);
    for(std::list<std::string>::iterator job = jobs.begin();job!=jobs.end();++job) {
      std::string line = "<LI><I>job</I> <A HREF=\"";
      line+=config.Endpoint()+"/"+(*job);
      line+="\">";
      line+=(*job);
      line+="</A>\r\n";
      html+=line;
    };
    html+="</UL>\r\n</BODY>\r\n</HTML>";
    Arc::PayloadRaw* buf = NULL;
    buf=new Arc::PayloadRaw;
    if(buf) buf->Insert(html.c_str(),0,html.length());
    outmsg.Payload(buf);
    outmsg.Attributes()->set("HTTP:content-type","text/html");
    return Arc::MCC_Status(Arc::STATUS_OK);
  };
  ARexJob job(id,config);
  if(!job) {
    // There is no such job
    logger_.msg(Arc::ERROR, "Get: there is no job %s - %s", id, job.Failure());
    // TODO: make proper html message
    return Arc::MCC_Status();
  };
  std::string session_dir = job.SessionDir();
  if(!http_get(outmsg,config.Endpoint()+"/"+id,session_dir,subpath,range_start,range_end)) {
    // Can't get file
    logger.msg(Arc::ERROR, "Get: can't process file %s/%s", session_dir, subpath);
    // TODO: make proper html message
    return Arc::MCC_Status();
  };
  return Arc::MCC_Status(Arc::STATUS_OK);
} 

static Arc::MCC_Status http_get(Arc::Message& outmsg,const std::string& burl,const std::string& bpath,std::string hpath,size_t start,size_t end) {
Arc::Logger::rootLogger.msg(Arc::DEBUG, "http_get: start=%u, end=%u, burl=%s, bpath=%s, hpath=%", (unsigned int)start, (unsigned int)end, burl, bpath, hpath);
  std::string path=bpath;
  if(!hpath.empty()) if(hpath[0] == '/') hpath=hpath.substr(1);
  if(!hpath.empty()) if(hpath[hpath.length()-1] == '/') hpath.resize(hpath.length()-1);
  if(!hpath.empty()) path+="/"+hpath;
  struct stat st;
  if(lstat(path.c_str(),&st) == 0) {
    if(S_ISDIR(st.st_mode)) {
      try {
        Glib::Dir dir(path);
        // Directory - html with file list
        std::string file;
        std::string html;
        html="<HTML>\r\n<HEAD>\r\n<TITLE>ARex: Job</TITLE>\r\n</HEAD>\r\n<BODY>\r\n<UL>\r\n";
	std::string furl = burl;
	if(!hpath.empty()) furl+="/"+hpath;
        for(;;) {
          file=dir.read_name();
          if(file.empty()) break;
          if(file == ".") continue;
          if(file == "..") continue;
          std::string fpath = path+"/"+file;
          if(lstat(fpath.c_str(),&st) == 0) {
            if(S_ISREG(st.st_mode)) {
              std::string line = "<LI><I>file</I> <A HREF=\"";
              line+=furl+"/"+file;
              line+="\">";
              line+=file;
              line+="</A> - "+Arc::tostring(st.st_size)+" bytes"+"\r\n";
              html+=line;
            } else if(S_ISDIR(st.st_mode)) {
              std::string line = "<LI><I>dir</I> <A HREF=\"";
              line+=furl+"/"+file+"/";
              line+="\">";
              line+=file;
              line+="</A>\r\n";
              html+=line;
            };
          };
        };
        html+="</UL>\r\n</BODY>\r\n</HTML>";
        Arc::PayloadRaw* buf = new Arc::PayloadRaw;
        if(buf) buf->Insert(html.c_str(),0,html.length());
        outmsg.Payload(buf);
        outmsg.Attributes()->set("HTTP:content-type","text/html");
        return Arc::MCC_Status(Arc::STATUS_OK);
      } catch(Glib::FileError& e) {
      };
    } else if(S_ISREG(st.st_mode)) {
      // File 
      PayloadFile* h = new PayloadFile(path.c_str(),start,end);
      outmsg.Payload(h);
      outmsg.Attributes()->set("HTTP:content-type","application/octet-stream");
      return Arc::MCC_Status(Arc::STATUS_OK);
    };
  };
  // Can't process this path
  // offset=0; size=0;
  return Arc::MCC_Status();
}

} // namespace ARex

