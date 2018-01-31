# gen_reg_api.py
# Extracts information about HLS IP for Vivado project.
# And generate register Setting API.
# tae.s.kim@intel.com

import xml.etree.ElementTree
import sys,os,re
from IPython import embed
from optparse import OptionParser
import string


if __name__ == "__main__":

    parser = OptionParser("Usage: %s [options] ./PATH_TO_/auxiliary.xml"%(sys.argv[0]))

    parser.add_option("-o", "--output", type="string", default="halide_zynq_api_setreg.cpp",
                      help="Output file name")
    parser.add_option("-d", "--debug", action="store_true", dest="debug",
                      help="Include debug printf")

    (options, file_list) = parser.parse_args() 

    foutname = options.output
    foutheader = os.path.splitext(options.output)[0]+".h"
    f_name = file_list[0]

    try:
        fout = open(foutname,'w')
    except:
        print >> sys.stderr, "Fail to open {}.".format(foutname)
        parser.print_help()
        sys.exit()

    try:
        fhead = open(foutheader,'w')
    except:
        print >> sys.stderr, "Fail to open {}.".format(foutname)
        parser.print_help()
        sys.exit()

    xd = '{http://www.xilinx.com/xidane}'

    if not os.path.isfile(f_name):
        print >> sys.stderr, "Missing file {}.".format(f_name)
        parser.print_help()
        sys.exit()

    root = xml.etree.ElementTree.parse(f_name).getroot()

    header = '''
#ifndef REGISTER_T_DEFINED
#define REGISTER_T_DEFINED
typedef struct hwacc_reg_t {
    unsigned int offset;
    unsigned int value;
} hwacc_reg_t;
#endif
#define SET_REG32 1005 // Set Configuratio register

extern int fd_hwacc;


'''
    print >> fhead, header
    print >> fout, "#include <stdio.h>"
    print >> fout, "#include <sys/ioctl.h>"
    print >> fout, "#include \"%s\"\n"%(foutheader)

    array_list = {}

    for reg in root.findall(xd+"arg"):
        name   = reg.get(xd+"name")
        orgname= reg.get(xd+"originalName")
        orgname= string.replace(orgname, "arg_", "")
        offset = reg.get(xd+"offset")
        size   = reg.get(xd+"arraySize")
        datawidth  = reg.get(xd+"dataWidth")
        bus    = reg.get(xd+"busTypeRef")

        if bus!="axilite": continue

        if name.endswith("]"): # Completely or partially decomposed array. Collect info only here and generate code later.
            if not orgname in array_list.keys():
                array_list[orgname] = {"offset": [], "size": [], "datawidth": datawidth}

            array_list[orgname]["offset"].append(offset)
            if size: # for partially decomposed array
                array_list[orgname]["size"].append(size)
            else: # for completely decomposed array (32 bit address)
                array_list[orgname]["size"].append(1)

        else: # scalar array
            if datawidth=="32":
                print >> fout, "int halide_zynq_set_%s(unsigned int %s) {"%(orgname,orgname)
                print >> fhead, "int halide_zynq_set_%s(unsigned int %s);"%(orgname,orgname)
            elif datawidth=="16":
                print >> fout, "int halide_zynq_set_%s(unsigned short %s) {"%(orgname,orgname)
                print >> fhead, "int halide_zynq_set_%s(unsigned short %s);"%(orgname,orgname)
            else: # datawidth=="8":
                print >> fout, "int halide_zynq_set_%s(unsigned char %s) {"%(orgname,orgname)
                print >> fhead, "int halide_zynq_set_%s(unsigned char %s);"%(orgname,orgname)

            if options.debug:
                print >> fout, "  printf(\"Setting %s\\n\");"%(orgname)

            print >> fout, "  hwacc_reg_t r;"
            print >> fout, "  if (fd_hwacc == 0) {"
            print >> fout, "      printf(\"Zynq runtime is uninitialized.\\n\");"
            print >> fout, "      return -1;"
            print >> fout, "  }"
            print >> fout, "  r.offset = (unsigned int)%s;"%(offset)
            print >> fout, "  r.value  = (unsigned int)(%s);"%(orgname)

            if options.debug:
                print >> fout, "    printf(\"  r.offset = %x\\n\", r.offset);"
                print >> fout, "    printf(\"  r.value  = %d\\n\", r.value );"

            print >> fout, "  ioctl(fd_hwacc, SET_REG32, &r);"
            print >> fout, "  return 0;"

            print >> fout, "}"

    for k in array_list.keys():
        orgname    = k
        datawidth  = array_list[k]["datawidth"]
        offset     = array_list[k]["offset"]
        size       = array_list[k]["size"]

        print >> fout, "int halide_zynq_set_%s(unsigned char *%s) {"%(orgname,orgname)
        print >> fhead, "int halide_zynq_set_%s(unsigned char *%s);"%(orgname,orgname)

        print >> fout, "  hwacc_reg_t r;"
        print >> fout, "  if (fd_hwacc == 0) {"
        print >> fout, "      printf(\"Zynq runtime is uninitialized.\\n\");"
        print >> fout, "      return -1;"
        print >> fout, "  }"

        if datawidth=="32":
            print >> fout, "  unsigned int * p = (unsigned int*)%s;"%(orgname)
        elif datawidth=="16":
            print >> fout, "  unsigned short int * p = (unsigned short int*)%s;"%(orgname)
        else: # widht==8
            print >> fout, "  unsigned char * p = (unsigned char *)%s;"%(orgname)

        for i in range(len(offset)):
            print >> fout, "  for(size_t i=0;i<%s;i++, p++) {"%(size[i])
            if datawidth=="32":
                print >> fout, "    r.offset = %s + (unsigned long int)p - (unsigned long int)%s;"%(offset[i], orgname)
                print >> fout, "    r.value  = (unsigned int)(*p);"
                print >> fout, "    ioctl(fd_hwacc, SET_REG32, &r);"
            elif datawidth=="16":
                print >> fout, "    if ((i&1)==0) {"
                print >> fout, "        r.value  = 0;"
                print >> fout, "        r.offset = %s + (unsigned long int)p - (unsigned long int)%s;"%(offset[i], orgname)
                print >> fout, "    }"
                print >> fout, "    r.value  |= (*(p+(i&1)))<<(16*(i&1));"
                print >> fout, "    if (((i%2)==1)||(i==(%s-1))) ioctl(fd_hwacc, SET_REG32, &r);"%(size[i])
            else: # widht==8
                print >> fout, "    if ((i&3)==0) {"
                print >> fout, "        r.value  = 0;"
                print >> fout, "        r.offset = %s + (unsigned long int)p - (unsigned long int)%s;"%(offset[i], orgname)
                print >> fout, "    }"
                print >> fout, "    r.value  |= (*(p+(i&3)))<<(8*(i&3));"
                print >> fout, "    if (((i&3)==3)||(i==(%s-1))) ioctl(fd_hwacc, SET_REG32, &r);"%(size[i])
            print >> fout, "  }"
        print >> fout, "  return 0;"
        print >> fout, "}"
        print >> fout, ""


