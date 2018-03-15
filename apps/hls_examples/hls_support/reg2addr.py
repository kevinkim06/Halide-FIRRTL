#!/usr/bin/env python

import sys,os,re
from optparse import OptionParser

if __name__ == "__main__":

    parser = OptionParser("Usage: %s [options] ./PATH_TO_/hls_target.fir"%(sys.argv[0]))

    parser.add_option("-m", "--map", type="string", default="hls_target.fir",
                      help="Generated FIRRTL file which contains register map in a commented section of module SlaveIF.")
    parser.add_option("-v", "--value", type="string", default="param.dat",
                      help="Configuration value of each register dumped during test.")
    parser.add_option("-o", "--output", type="string", default="param_addr.dat",
                      help="Output file name")
    parser.add_option("-d", "--debug", action="store_true", dest="debug",
                      help="Include debug printf")

    (options, file_name) = parser.parse_args() 

    #import pdb;pdb.set_trace()

    # Extract register map table from the generated FIRRTL file
    reg_map = {}
    reg_map_pattern = re.compile("; 0x(\w+) : (\w+)$")
    reg_map_begin = False
    with open(options.map) as f:
        for line in f:
            line = line.strip()
            #print line
            map_start = re.search("Start of Register Map", line)
            if not reg_map_begin : # forward until map starts
                if map_start: reg_map_begin = True
                continue
            map_end = re.search("End of Register Map", line)
            if reg_map_begin :
                if map_end: break # done

            m = reg_map_pattern.match(line)
            if m :
                #print line
                reg_map[m.group(2)] = m.group(1)

    f.close()

    #for k in reg_map.keys():
    #    print k, reg_map[k]

    # Read configuration value from the generated param.dat dumped during test run.
    # and convert to physical offset file.
    value_pattern = re.compile("(\w+) (-?\w+)$")
    with open(options.value) as f:
        with open(options.output, "w") as fw:
            for line in f:
                line = line.strip()
                m = value_pattern.match(line)
                if m :
                    #print reg_map[m.group(1)], m.group(2)
                    fw.write("%s %s\n"%(reg_map[m.group(1)], m.group(2)))

    f.close()
    fw.close()

