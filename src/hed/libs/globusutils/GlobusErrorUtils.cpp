#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <arc/StringConv.h>

#include "GlobusErrorUtils.h"

namespace Arc {

  std::ostream& operator<<(std::ostream& o, const GlobusResult& res) {
    if (res)
      return (o << "<success>");
    globus_object_t *err = globus_error_get(res);
    for (globus_object_t *err_ = err; err_;
	 err_ = globus_error_base_get_cause(err_)) {
      if (err_ != err)
	o << "/";
      char *tmp = globus_object_printable_to_string(err_);
      if (tmp) {
	o << tmp;
	free(tmp);
      }
      else
	o << "unknown error";
    }
    if (err)
      globus_object_free(err);
    return o;
  }

  std::string GlobusResult::str() const {
    if (r == GLOBUS_SUCCESS)
      return "<success>";
    globus_object_t *err = globus_error_get(r);
    std::string s;
    for (globus_object_t *err_ = err; err_;
	 err_ = globus_error_base_get_cause(err_)) {
      if (err_ != err)
	s += "/";
      char *tmp = globus_object_printable_to_string(err_);
      if (tmp) {
	s += tmp;
	free(tmp);
      }
      else
	s += "unknown error";
    }
    if (err)
      globus_object_free(err);
    return trim(s);
  }

  std::ostream& operator<<(std::ostream& o, globus_object_t *err) {
    if (err == GLOBUS_NULL)
      return (o << "<success>");
    for (globus_object_t *err_ = err; err_;
	 err_ = globus_error_base_get_cause(err_)) {
      if (err_ != err)
	o << "/";
      char *tmp = globus_object_printable_to_string(err_);
      if (tmp) {
	o << tmp;
	free(tmp);
      }
      else
	o << "unknown error";
    }
    return o;
  }

  std::string globus_object_to_string(globus_object_t *err) {
    if (err == GLOBUS_NULL)
      return "<success>";
    std::string s;
    for (globus_object_t *err_ = err; err_;
	 err_ = globus_error_base_get_cause(err_)) {
      if (err_ != err)
	s += "/";
      char *tmp = globus_object_printable_to_string(err_);
      if (tmp) {
	s += tmp;
	free(tmp);
      }
      else
	s += "unknown error";
    }
    return s;
  }

  int globus_error_to_errno(const std::string& msg, int errorno) {
    // parse the message and try to detect certain errors. If none found leave
    // errorno unchanged. There is no guarantee that Globus won't change error
    // messages but there is no other way to determine the reason for errors.
    if (lower(msg).find("no such file")         != std::string::npos) return ENOENT;
    if (lower(msg).find("object unavailable")   != std::string::npos) return ENOENT;
    if (lower(msg).find("object not available") != std::string::npos) return ENOENT;
    if (lower(msg).find("no such job")          != std::string::npos) return ENOENT;
    if (lower(msg).find("file unavailable")     != std::string::npos) return ENOENT;
    if (lower(msg).find("file exists")          != std::string::npos) return EEXIST;
    if (lower(msg).find("file not allowed")     != std::string::npos) return EACCES;
    if (lower(msg).find("permission denied")    != std::string::npos) return EACCES;
    if (lower(msg).find("failed authenticating")!= std::string::npos) return EACCES;
    if (lower(msg).find("can't make")           != std::string::npos) return EACCES;
    if (lower(msg).find("directory not empty")  != std::string::npos) return ENOTEMPTY;
    if (lower(msg).find("do not understand")    != std::string::npos) return EOPNOTSUPP;

    return errorno;
  }

} // namespace Arc
