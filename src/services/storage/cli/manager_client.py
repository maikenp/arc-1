import arc, sys, time, os
from storage.common import false
from storage.xmltree import XMLTree
from storage.client import ManagerClient, ByteIOClient
from storage.common import create_checksum
args = sys.argv[1:]
if len(args) > 0 and args[0] == '-x':
    args.pop(0)
    print_xml = True
else:
    print_xml = False
manager = ManagerClient('http://localhost:60000/Manager', print_xml)
if len(args) == 0 or args[0] not in ['stat', 'makeCollection', 'list', 'move', 'putFile', 'getFile', 'addReplica']:
    print 'Supported methods: stat, makeCollection, list, move, putFile, getFile, addReplica' 
else:
    command = args.pop(0)
    if command == 'stat':
        if len(args) < 1:
            print 'Usage: stat <LN> [<LN> ...]'
        else:
            request = dict([(i, args[i]) for i in range(len(args))])
            print 'stat', request
            stat = manager.stat(request)
            print stat
            for i,s in stat.items():
                print '%s:' % args[int(i)]
                c = {}
                for k,v in s.items():
                    sect, prop = k
                    c[sect] = c.get(sect,[])
                    c[sect].append((prop, v))
                for k, vs in c.items():
                    print k
                    for p, v in vs:
                        print '  %s: %s' % (p, v)
    elif command == 'getFile':
        if len(args) < 2:
            print 'Usage: getFile <target filename> <source LN>'
        else:
            LN = args[1]
            filename = args[0]
            f = file(filename, 'wb')
            request = {'0' : (LN, ['byteio'])}
            print 'getFile', request
            response = manager.getFile(request)
            print response
            success, turl, protocol = response['0']
            print '\n', LN, success
            if success == 'done':
                print 'Downloading from', turl, 'to', filename
                ByteIOClient(turl).read(file = f)
    elif command == 'addReplica':
        if len(args) < 2:
            print 'Usage: addReplica <source filename> <GUID>'
        else:
            filename = args[0]
            requests = {'0' : args[1]}
            protocols = ['byteio']
            print 'addReplica', requests, protocols
            response = manager.addReplica(requests, protocols)
            print response
            success, turl, protocol = response['0']
            if success == 'done':
                f = file(filename,'rb')
                print 'Uploading from', filename, 'to', turl
                ByteIOClient(turl).write(f)
    elif command == 'putFile':
        if len(args) < 2:
            print 'Usage: putFile <source filename> <target LN>'
        else:
            filename = args[0]
            size = os.path.getsize(filename)
            f = file(filename,'rb')
            checksum = create_checksum(f, 'md5')
            LN = args[1]
            metadata = {('states', 'size') : size, ('states', 'checksum') : checksum,
                    ('states', 'checksumType') : 'md5', ('states', 'neededReplicas') : 2}
            request = {'0': (LN, metadata, ['byteio'])}
            print 'putFile', request
            response = manager.putFile(request)
            print response
            success, turl, protocol = response['0']
            print '\n', LN, success
            if success == 'done':
                f = file(filename,'rb')
                print 'Uploading from', filename, 'to', turl
                ByteIOClient(turl).write(f)
    elif command == 'makeCollection':
        if len(args) < 1:
            print 'Usage: makeCollection <LN>'
        else:
            request = {'0': (args[0], {('states', 'closed') : false})}
            print 'makeCollection', request
            response = manager.makeCollection(request)
            print response
            print '\n', response['0']
    elif command == 'list':
        if len(args) < 1:
            print 'Usage: list <LN> [<LN> ...]'
        else:
            request = dict([(str(i), args[i]) for i in range(len(args))])
            print 'list', request
            response = manager.list(request,[('catalog','')])
            print response
            for rID, (entries, status) in response.items():
                print
                if status == 'found':
                    print '%s:' % request[rID]
                    for name, (GUID, metadata) in entries.items():
                        print '\t%s\t<%s>' % (name, metadata.get(('catalog', 'type'),'unknown'))
                else:
                    print '%s: %s' % (request[rID], status)
    elif command == 'move':
        if len(args) < 2:
            print 'Usage: move <sourceLN> <targetLN> [preserve]'
        else:
            sourceLN = args.pop(0)
            targetLN = args.pop(0)
            preserveOriginal = False
            if len(args) > 0:
                if args[0] == 'preserve':
                    preserveOriginal = True
            request = {'0' : (sourceLN, targetLN, preserveOriginal)}
            print 'move', request
            response = manager.move(request)
            print response
            print '\n', response['0'][0]
