#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <arc/StringConv.h>

#include "ConfigUtils.h"
#include "../misc/escaped.h"

namespace ARex {

bool config_open(std::ifstream &cfile,const std::string &name) {
  cfile.open(name.c_str(),std::ifstream::in);
  return cfile.is_open();
}

bool config_close(std::ifstream &cfile) {
  if(cfile.is_open()) cfile.close();
  return true;
}

std::string config_read_line(std::istream &cfile,std::string &rest,char separator) {
  rest = config_read_line(cfile);
  return config_next_arg(rest,separator);
}

std::string config_read_line(std::istream &cfile) {
  std::string rest;
  for(;;) {
    if(cfile.eof() || cfile.fail()) { rest=""; return rest; };
    std::getline(cfile,rest);
    Arc::trim(rest," \t\r\n");
    if(rest.empty()) continue; /* empty string - skip */
    if(rest[0] == '#') continue; /* comment - skip */
    break;
  };
  return rest;
}

std::string config_next_arg(std::string &rest,char separator) {
  int n;
  std::string arg;
  n=input_escaped_string(rest.c_str(),arg,separator);
  rest=rest.substr(n);
  return arg;
}    

config_file_type config_detect(std::istream& in) {
  char inchar;
  if (!in.good()) return config_file_unknown;
  while(in.good()) {
    inchar = (char)(in.get());
    if(isspace(inchar)) continue;
    if(inchar == '<') {
      // XML starts from < even if it is comment
      in.putback(inchar);
      return config_file_XML;
    };
    if((inchar == '#') || (inchar = '[')) {
      // INI file starts from comment or section
      in.putback(inchar);
      return config_file_INI;
    };
  };
  in.putback(inchar);
  return config_file_unknown;
}


bool elementtobool(Arc::XMLNode pnode,const char* ename,bool& val,Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  if((v == "true") || (v == "1")) {
    val=true;
    return true;
  };
  if((v == "false") || (v == "0")) {
    val=false;
    return true;
  };
  if(logger && ename) logger->msg(Arc::ERROR,"wrong boolean in %s: %s",ename,v.c_str());
  return false;
}

bool elementtoint(Arc::XMLNode pnode,const char* ename,unsigned int& val,Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  if(Arc::stringto(v,val)) return true;
  if(logger && ename) logger->msg(Arc::ERROR,"wrong number in %s: %s",ename,v.c_str());
  return false;
}

bool elementtoint(Arc::XMLNode pnode,const char* ename,int& val,Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  if(Arc::stringto(v,val)) return true;
  if(logger && ename) logger->msg(Arc::ERROR,"wrong number in %s: %s",ename,v.c_str());
  return false;
}

bool elementtoint(Arc::XMLNode pnode,const char* ename,time_t& val,Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  if(Arc::stringto(v,val)) return true;
  if(logger && ename) logger->msg(Arc::ERROR,"wrong number in %s: %s",ename,v.c_str());
  return false;
}

bool elementtoint(Arc::XMLNode pnode,const char* ename,unsigned long long int& val,Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  if(Arc::stringto(v,val)) return true;
  if(logger && ename) logger->msg(Arc::ERROR,"wrong number in %s: %s",ename,v.c_str());
  return false;
}

bool elementtoenum(Arc::XMLNode pnode,const char* ename,int& val,const char* const opts[],Arc::Logger* logger) {
  std::string v = ename?pnode[ename]:pnode;
  if(v.empty()) return true; // default
  for(int n = 0;opts[n];++n) {
    if(v == opts[n]) { val = n; return true; };
  }; 
  return false;
}

} // namespace ARex