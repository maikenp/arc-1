#!/usr/bin/python

"""Usage: DGAuthplug.py <status> <control dir> <runtime dir> <jobid>

Authplugin for ACCEPTING state in 3G-Bridge

Example:

  authplugin="ACCEPTED timeout=60,onfailure=fail,onsuccess=log /usr/libexec/arc/3GAuthplug.py %S %C /var/spool/nordugrid/runtime %I"

"""

def ExitError(msg,code):
    """Print error message and exit"""
    from sys import exit
    print(msg)
    exit(code)

def WriteLog(msg,logfile,username):
    """Write the logfile"""
    from fcntl import flock, LOCK_EX, LOCK_UN, LOCK_NB
    from datetime import datetime
    import pwd,os


    # We should have logging and locking
    fd = open(logfile, 'a')

    # Set correct ownership if this plugin (and thus A-REX) create the file
    (pw_name,pw_passwd,pw_uid,pw_gid,pw_gecos,pw_dir,pw_shell) = pwd.getpwnam(username)
    os.chown(logfile,pw_uid,pw_gid)

    try:
        flock(fd, LOCK_EX)
    except IOError:
        fd.close()
        ExitError('Failed to lock',1)
    else:
        date = datetime.now().strftime("%Y-%m-%d_%H:%M:%S")
        fd.write("dt=%s %s\n"%(date,msg))
        fd.close()

def GetVOs(control_dir,jobid):
    """Return an array of VO
    TODO: support arcproxy 
    """
    from subprocess import Popen,PIPE
    from os.path import isfile

    proxy_file = '%s/job.%s.proxy' %(control_dir,jobid)

    if not isfile(proxy_file):
       ExitError("No such proxy: %s"%proxy_file,1)

    cmd1 = ["voms-proxy-info","-all","-file",proxy_file]
    cmd2 = ["grep", "^VO "]
    p1 = Popen(cmd1, stdout=PIPE)
    p2 = Popen(cmd2, stdin=p1.stdout, stdout=PIPE)
    stdout,stderr = p2.communicate()
    lines = stdout.split('\n')
    vos = []
    for line in lines:
        if line:
            vo = line.split(':')[1].strip()
            vos.append(vo)
    return vos

def GetPrimaryVO(control_dir,jobid):

    """Return the first VO (if any) as a simle string"""

    VOs = GetVOs(control_dir,jobid)
    if len(VOs) >0:
       return VOs[0]
    else:
       return ""
    return GetVOs(control_dir,jobid)[0]

def GetDescription(control_dir,jobid):

    from os.path import isfile

    desc_file = '%s/job.%s.description' %(control_dir,jobid)
    
    if not isfile(desc_file):
       ExitError("No such description file: %s"%desc_file,1)

    f = open(desc_file)
    desc = f.read()
    f.close()

    return desc

def GetRTEs(desc):

    """Return array with RTEs"""

    import arc
    jd = arc.JobDescription()
    jd.Parse(desc)
    rtes = []
    for i in jd.Resources.RunTimeEnvironment.getSoftwareList():
       rtes.append(i.getName())
    return rtes

def GetAllowedVOs(runtime_dir,rte):

    """Find allowed VOs from site RTE"""

    from os.path import isfile

    rte_file = "%s/%s" % (runtime_dir,rte)
    
    # RTE should be available
    if not isfile(rte_file):
       ExitError("No such RTE: %s"%rte,1)

    f = open(rte_file)
    rte_script = f.readlines()
    f.close()

    for l in rte_script:
        # Remove newlines
	l = l.strip()
        if l[0:14] == 'DG_AllowedVOs=':
            l = l.split("=")[1]
            vos = l[1:-1].split(" ")

    return vos

def GetUniqueRTE(desc):

    """Return unique RTE from job description"""

    import sys
    rtes = GetRTEs(desc)
    # Make sure there is only 1 RTE specified
    if len(rtes) != 1:
        ExitError("Number of RTEs should be 1 - not: %s" % len(rtes),1)
    return rtes[0]

def IsAccepted(control_dir,runtime_dir,jobid):

    # Find the primary VO
    vo = GetPrimaryVO(control_dir,jobid)

    # Find requested runtime environments
    desc = GetDescription(control_dir,jobid)
    rte = GetUniqueRTE(desc)

    allowed_vos = GetAllowedVOs(runtime_dir,rte)
    if vo not in allowed_vos and 'all' not in allowed_vos:
#        ExitError("You are not allowed",1)
#        ExitError("Your vo %s are not among the allowed: %s"%(vo,allowed_vos),1)
        if not vo:
            ExitError("You must be assigned to one of the allowed VOs",1)
        else:
            ExitError("Your vo <%s> is not among the allowed VOs for %s"%(vo,rte),1)

    return True

def ParseRTE(runtime_dir,rte):

    """Parse RTE an return as dictionary"""

    import ConfigParser
#    import io
    import StringIO
    import sys
    from os.path import isfile

    # Find VOs
    rte_file = "%s/%s" % (runtime_dir,rte)
    
    # RTE should be available
    if not isfile(rte_file):
        ExitError("No such RTE: %s"%rte,1)

    f = open(rte_file)
    str = f.read()
    f.close()
    str = "[s]\n" + str

    config = ConfigParser.RawConfigParser()
#    config.readfp(io.BytesIO(str))
    strfp = StringIO.StringIO(str)
    config.readfp(strfp)

    d = {}
    for i in config.items('s'):
        d[i[0]] = i[1].strip('"')
    return d

def DoLog(control_dir,runtime_dir,jobid,username):

    """Log the state"""
    import datetime

    # Logfile is in <control_dir>/3gbridge_logs/YYYY-MM-DD
    logfile = "%s/3gbridge_logs/%s" % (control_dir,datetime.date.today().isoformat())
    desc = GetDescription(control_dir,jobid)
    rte = GetUniqueRTE(desc)
    rte_dict = ParseRTE(runtime_dir,rte)
    application = rte_dict['dg_app']

    vo = GetPrimaryVO(control_dir,jobid)

    WriteLog("event=job_entry job_id=%(jobid)s application=%(application)s input_grid_name=ARC/%(vo)s"%locals(),logfile,username)

    return True

def main():
    """Main"""

    import sys

    # Parse arguments

    if len(sys.argv) != 6:
        ExitError("Too few arguments\n"+__doc__,1)

    (exe, status, control_dir, runtime_dir, jobid, username) = sys.argv

    if status == "ACCEPTED" and IsAccepted(control_dir,runtime_dir,jobid):
        sys.exit(0)

    if status == "PREPARING" and DoLog(control_dir,runtime_dir,jobid,username):
        sys.exit(0)
        
    sys.exit(1)

if __name__ == "__main__":
    main()

