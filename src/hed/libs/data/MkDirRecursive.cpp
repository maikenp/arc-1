// -*- indent-tabs-mode: nil -*-

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WIN32
#include <arc/win32.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
// NOTE: On Solaris errno is not working properly if cerrno is included first
#include <cerrno>

#include <iostream>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glibmm.h>

#include <arc/data/MkDirRecursive.h>

static int mkdir_force(const char *path, mode_t mode);

// TODO: proper solution for windows
int mkdir_recursive(const std::string& base_path, const std::string& path,
                    mode_t mode, const Arc::User& user) {
  std::string name = Glib::build_filename(base_path,path);
  std::string::size_type name_start = base_path.length();
  std::string::size_type name_end = name.length();
#ifdef WIN32
  // Skip disk:/ part of path if exists
  if(Glib::path_is_absolute(name)) {
    name_start = name_end - Glib::path_skip_root(name).length();
  }  
#endif
  /* go down */
  for (;;) {
    if ((mkdir_force(name.substr(0, name_end).c_str(), mode) == 0) ||
        (errno == EEXIST)) {
      if (errno != EEXIST)
        (lchown(name.substr(0, name_end).c_str(), user.get_uid(),
                user.get_gid()) != 0);
      /* go up */
      for (;;) {
        if (name_end >= name.length())
          return 0;
        name_end = name.find(G_DIR_SEPARATOR, name_end + 1);
        if (mkdir(name.substr(0, name_end).c_str(), mode) != 0) {
          if (errno == EEXIST)
            continue;
          return -1;
        }
        chmod(name.substr(0, name_end).c_str(), mode);
        (lchown(name.substr(0, name_end).c_str(), user.get_uid(),
                user.get_gid()) != 0);
      }
    }
    /* if(errno == EEXIST) { free(name); errno=EEXIST; return -1; } */
    if ((name_end = name.rfind(G_DIR_SEPARATOR, name_end - 1)) ==
        std::string::npos)
      break;
    if (name_end == name_start)
      break;
  }
  return -1;
}

int mkdir_force(const char *path, mode_t mode) {
  struct stat st;
  int r;
  if (stat(path, &st) != 0) {
    r = mkdir(path, mode);
    if (r == 0)
      (void)chmod(path, mode);
    return r;
  }
  if (S_ISDIR(st.st_mode)) { /* simulate error */
    r = mkdir(path, mode);
    if (r == 0)
      (void)chmod(path, mode);
    return r;
  }
  if (remove(path) != 0)
    return -1;
  r = mkdir(path, mode);
  if (r == 0)
    (void)chmod(path, mode);
  return r;
}
